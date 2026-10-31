# Cloud AutoScalar

C++20/libvirt implementation of Assignment 1. It runs a concurrent Catalan
computation service, monitors domain CPU, and scales **1 → 2 → 1** replicas.
Runtime monitoring, provisioning, and deletion call libvirt directly.

## Build and test

Debian/Ubuntu host dependencies:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake pkg-config libvirt-dev \
  libvirt-daemon-system libvirt-clients qemu-system-x86 qemu-utils python3 python3-matplotlib
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Build outputs: `cloud_autoscalar`, `cloud_autoscalar_client`, `catalan-server`.
Tests bind loopback sockets and require port 8080 to be free. They cover config
validation, exact arithmetic, CPU policy, concurrent routing, continuous client
modes, clone/readiness failures, draining, partial deletion, and restart recovery.
The integration fixtures use mock guests; a successful CTest run is not a real
VM scaling demonstration.

## Prepare the guest and backing image

The sample config targets the existing `ubuntu_base` domain, the `default` NAT
network and storage pool, and a QCOW2 backing image at
`/var/lib/libvirt/images/autoscaler-base.qcow2`. Each server has **1 vCPU and
1024 MiB RAM**. The initial VM is pre-existing; replicas get separate overlays.
The initial disk must never be used as a live writable backing image.

1. Enable KVM/libvirt and the default NAT network. If missing, define the network
   from `/usr/share/libvirt/networks/default.xml`. Give your user access to
   `qemu:///system` through the host's libvirt/polkit configuration.
2. Copy `computation/Catalan.cpp`, `computation/Catalan.hpp`,
   `computation/catalan-server.service`, `commons/httplib.h`, and
   `tools/provision_guest.sh` into one directory in the Ubuntu guest. Run
   `sh provision_guest.sh .` there. It builds the worker and enables its systemd
   service. Any previous `cloud-worker` service is disabled to avoid port conflicts.
3. Configure persistent netplan DHCP matching `en*` rather than a particular
   interface name/MAC. Set `dhcp-identifier: mac` so clones do not reuse a copied
   DHCP client identity. Disable cloud-init network regeneration if it owns the
   netplan file. Check that both the original and a clone obtain distinct leases.
4. Verify `curl http://GUEST_IP:8080/health` returns `workload: catalan` and
   `curl 'http://GUEST_IP:8080/binary-trees?n=10'` returns `16796`.
5. Run `python3 tools/prepare_image.py autoscaler.config`. This requests a clean
   shutdown, creates an offline libvirt volume copy, and restarts the original VM.
   It refuses an existing base and never forces poweroff. Treat the copied base as
   immutable. Refreshing/replacing a base requires removing dependent overlays
   and preparing a new image explicitly.

Clones use BIOS/Q35, a virtio disk/NIC, and the `default` network, matching the
current initial VM. Hardware virtualization and sufficient host RAM are required.
The guest service has no artificial memory cap. Inputs 0–100 are accepted by the
Catalan server, but the deliberately naive recursion is exponential: large inputs
may never finish in useful time. **Catalan counting is different from materializing
one full binary tree.** The earlier full-tree worker remains an optional experiment
under `cloud-autoscalar/worker`; do not run both services on port 8080.

## Configuration and running

`autoscaler.config` is JSON. Relative state/metrics paths resolve against the
configuration file directory, independent of the process working directory.
The defaults use 1 initial/2 maximum VMs, thresholds 75%/25%, three overloaded or
five underloaded samples, 2-second sampling, 30-second warmup/cooldown, a 120-second
readiness deadline, and a 60-second drain deadline. Configure backend request and
client timeouts separately; the client deadline must be longer.

```bash
./build/cloud_autoscalar autoscaler.config
./build/cloud_autoscalar_client autoscaler.config
```

The client accepts `increase`, `decrease`, `stop`, `exit`. Modes remain active
until changed. They use configured rates and a fixed-size worker pool/bounded
queue, so adding a replica does not automatically increase offered load. Queued
requests from an old mode are discarded and counted when modes change. Active
requests finish under the configured deadline.

HTTP interfaces:

- Host `POST /work {"value":14}` → JSON status, selected backend, and result.
- Host `GET /replicas` → membership version, ready replicas, all backend states.
- Host and guest `GET /health`; guest response identifies the Catalan workload.
- Guest `GET /binary-trees?n=14` → exact decimal Catalan result. Integer validation
  rejects trailing garbage, negatives, values above 100, and overflowed inputs.

The client polls membership every two seconds and reports additions/removals.
The load balancer handles routing updates behind the stable client endpoint.
Invalid work returns 400, no ready backend 503, connection failure 502, and read
failure/timeout 504. Requests are not retried automatically after a timeout.

## Lifecycle safety and recovery

The controller samples CPU separately from its serialized lifecycle worker.
The first sample, stopped domains, CPU counter resets, and errors do not count as
low utilization. Scale-out publishes a VM only after checking all reported DHCP addresses for successful
HTTP health, including when a stale lease is returned first. Scale-in removes eligibility, drains active requests, then destroys,
undefines, and deletes the owned overlay. A drain timeout cancels removal.

A durable JSON journal records resource names, ownership, and cleanup phases;
created domains have matching libvirt ownership metadata. Startup adopts healthy
owned replicas and retries incomplete cleanup. Failed cleanup retains its journal
entry and occupies capacity, avoiding silent leaks or excess VMs. A separate
process lock prevents two controllers from using one journal. Name collisions
with unrelated domains are refused. The initial VM and immutable base are never
scale-in targets. Keep the journal if you intend to restart with existing replicas.

SIGINT/SIGTERM stops traffic and scaling, completes or rolls back in-progress
lifecycle work, and leaves healthy VMs available for restart. No automatic deletion
of the initial VM, user disks, or immutable template occurs.

## Calibrate and run the demonstration

Calibrate with the guest idle and before starting the client:

```bash
python3 tools/calibrate.py autoscaler.config --guest 192.168.122.86
python3 tools/demo.py demo.config --build build
python3 tools/plot.py runs/demo-TIMESTAMP
```

Calibration reuses the existing recursive workload, chooses a modest input, and
measures CPU seconds/request through libvirt. It writes `demo.config` with the
same input in both modes and rates targeting approximately 90%/9% of one VM.
The high rate stays fixed after scale-out, ideally giving about 45% per replica.
Only one replica should be added; sample config maximum is two. Recalibrate when
changing host, guest, compiler optimization, or workload code.

The demo creates its own orchestrator/client (or use `--attach` for an existing
orchestrator). It performs low load for 60 seconds, high load until the second VM
is ready plus 60 seconds, then low load until scale-in plus 30 seconds. Each scaling
phase has a five-minute deadline. It verifies both VM resource sizes, requests
reaching both replicas, successful requests, and lower initial-VM CPU after scale-out.
Use `--restart-check` to additionally pause new requests, restart the orchestrator
with two replicas, verify adoption, and resume the same high rate for 60 seconds.
Each run needs a fresh metrics directory. A failure leaves diagnostic logs and a
`summary.json` with `success: false`; it does not delete unrelated resources.

CSV output includes domain CPU, lifecycle events, per-request status/backend/latency,
and client mode/membership/drop events. Plots export PNG and SVG. Domain CPU includes
hypervisor execution attributed to the domain and can differ from guest process
CPU (`top`); explain sampling and virtualization overhead during the viva.

## Compatibility with the pulled metrics/cooldown commits

The existing `scaling.scale_in_enabled`, `scaling.cooldownSeconds`,
`traffic.high_request_delay_milliseconds`, and `metrics.file_path` keys remain
supported. `cooldown_seconds` is the preferred spelling; conflicting aliases are
rejected. Continuous traffic uses explicit `high_rate`/`low_rate` when provided;
otherwise legacy delay fields derive rates. Finite request-count fields are
accepted for old configs but no longer stop a continuous traffic mode.

Legacy CSV schemas remain available: `metrics.file_path` contains
`timestamp_ms,vm_name,cpu_percent,replica_count`, and the sibling
`request_metrics.csv` contains
`timestamp_ms,backend_ip,latency_ms,success,http_status`. Existing
`plot_metrics.py` and committed `images/` examples are preserved. Run it from the
project root for the default `results/` files, or pass `--results-dir RUN_DIRECTORY`; install `tools/requirements.txt`
for its pandas/matplotlib dependencies. Calibrated runs put compatible CSVs in
their run directory as well as the newer detailed CSV files.

## Submission

```bash
python3 tools/package.py GROUP_NUMBER --results runs/demo-TIMESTAMP
```

The archive is named `GROUP_NUMBER.tar.gz`. Packaging includes source, tests,
config examples, service definitions, tooling, and selected run results. It excludes
credentials, binaries, VM images, ISOs, and lifecycle journals. Upload it manually
to Eduserver; the group number is supplied when packaging.
