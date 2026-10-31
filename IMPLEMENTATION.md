# Implementation and file changes

Implemented on top of pulled commit `f1a1cf6` (performance metrics), including
`18195c2` (cooldown). The source is in `/home/ssladmin/Automatic-VM-Scalar`.
Changes are left uncommitted for review.

## Configuration and build

- `CMakeLists.txt`: builds shared controller code, orchestrator, continuous client,
  Catalan worker, and test fixtures; registers CTest integration tests.
- `commons/Config.hpp`, `commons/Config.cpp`, `autoscaler.config`: align the initial
  domain/pool/image with this host; use 1 CPU/1024 MiB and a two-replica limit;
  validate types/ranges; configure rates, queue capacity, timeouts, state, metrics,
  warmup/cooldown, readiness, and draining.
- Compatibility: retain the pulled `scale_in_enabled`, `cooldownSeconds`, request
  delay fields, and `metrics.file_path`; conflicting aliases are rejected.
- `.gitignore`: excludes generated builds, journals, run data, local configs,
  Python caches, plots/results, and archives.

## VM lifecycle

- `autoscaler/Autoscaler.hpp`, `.cpp`: use a serialized lifecycle worker while CPU
  monitoring continues; commit replicas only after readiness; persist/reconcile
  ownership and cleanup phases; support cooldown, draining, retry, and shutdown.
- New `autoscaler/Driver.hpp`, `.cpp`: encapsulate libvirt provisioning, ownership
  metadata, storage, domain removal, health, and address discovery. All reported
  addresses are health-checked because this host has both stale and current leases.
- Removed `autoscaler/DiskManager.hpp`, `.cpp` and `VMCloner.hpp`, `.cpp`: their disk
  and domain operations are now coordinated by `Driver`. This removed source
  wrappers, not the initial VM's virtual disk.
- `autoscaler/VMManager.hpp`, `.cpp`: atomically select/reserve backends, count
  in-flight requests, drain, track states, and publish versioned membership.
- New `autoscaler/Policy.hpp`: consecutive-sample thresholds, warmup/cooldown, and
  invalid-sample resets.
- `autoscaler/VMMonitor.hpp`, `.cpp`: mark first/reset/stopped measurements invalid
  and expose/test CPU delta arithmetic with vCPU normalization.
- `main.cpp`, `orchestrator/Orchestrator.hpp`, `.cpp`: preflight, process locking,
  separate libvirt handles, nonzero startup failures, graceful signal shutdown.

## Requests, workload, and traffic

- `server/LoadBalancer.hpp`, `.cpp`: concurrent round robin, request leases,
  membership/health endpoints, computed results, error classification, detailed
  and backward-compatible request metrics.
- `client/Client.cpp`, `client/LoadGenerator.hpp`, `.cpp`: continuous switchable
  modes, fixed offered rate, bounded queue, request pool, membership polling,
  request metrics, failures and drops.
- `computation/Catalan.cpp`: retain the existing expensive recursive computation;
  validate complete integer inputs 0–100, provide health, configurable thread count,
  exact results, and a nonzero bind-failure exit.
- New `computation/Catalan.hpp`: unsigned arbitrary-precision arithmetic, avoiding
  signed result overflow without an extra Boost dependency. The naive recursion
  is preserved; large accepted inputs remain impractical.
- New `computation/catalan-server.service`: unprivileged automatic guest startup.
- The previous full-tree/memory-growth experiment remains separately available in
  `cloud-autoscalar/worker`. The assignment demo uses the existing Catalan workload.

## Metrics, tooling, and tests

- New `commons/Metrics.hpp`: synchronized CSV output, including legacy millisecond
  timestamp formats.
- `plot_metrics.py`: preserve the pulled plotter; support custom result directories,
  repeated 1→2→1 transitions, and a shared time origin.
- New `tools/calibrate.py`, `libvirt_api.py`: choose a practical existing workload
  and derive fixed low/high rates using libvirt CPU readings.
- New `tools/demo.py`: run a repeatable 1→2→1 experiment, verify request success,
  resource sizes and lower CPU; optionally restart with two replicas and verify
  adoption before resuming traffic.
- New `tools/plot.py`, `requirements.txt`: export CPU/latency/throughput charts as
  PNG and SVG; include zero-request time buckets.
- New `tools/provision_guest.sh`, `prepare_image.py`: guest service installation
  and a clean offline image copy through libvirt, refusing to overwrite a base.
- New `tools/package.py`: package current source, config, services, tests, tooling,
  existing example images, and selected results; exclude VM disks and runtime state.
- New `tests/core_tests.cpp`: policy, CPU arithmetic, exact values beyond signed
  64-bit range, registry concurrency/draining, config and compatibility,
  ownership collisions, clone/readiness/removal failures, and restart recovery.
- New `tests/http_integration.py`, `proxy_fixture.cpp`, `proxy_integration.py`:
  workload correctness/validation, HTTP failures, concurrent routing, membership,
  continuous modes, and legacy metrics schema.
- `README.md`: replaces outdated setup and limitations with current architecture,
  provisioning, configuration, demo, recovery, compatibility, and submission steps.

## Host and guest setup performed

The original `ubuntu_base` remains on `/var/lib/libvirt/images/ubuntu_base.qcow2`.
The guest now runs the Catalan service instead of the earlier full-tree service.
Netplan matches `en*` and uses a MAC-based DHCP client ID, allowing clone addresses
to differ. The initial VM's working address is currently `192.168.122.87`.

Created `/var/lib/libvirt/images/autoscaler-base.qcow2` as an offline volume copy
with the guest shut down, then restarted the initial VM. Runtime replicas use
separate owned overlays. The old lease `.86` is still reported by libvirt, so
runtime discovery checks health rather than trusting lease ordering.

Built binaries are in `build/`. Plot dependencies for this session are installed
in `/tmp/cloud-autoscalar-python`; use the README dependencies for a permanent
installation, or prefix plotting commands with
`PYTHONPATH=/tmp/cloud-autoscalar-python` in the current session.

## Validation

All three CTest suites pass. The first real 1→2→1 cycle completed 695 requests
without failures and reduced initial-VM average CPU from 85.0% to 44.7% after
scale-out. Its evidence is in `runs/demo-20261031-225231/`.

The final combined version completed a second real cycle, including restarting
with two replicas and adopting the owned replica. It completed **1199 requests
with no failures**, reducing initial-VM mean CPU from **89.0% to 45.5%**. Evidence:
`runs/demo-20261031-230753/summary.json`, CSVs, logs, and plots. The replica was
subsequently drained/removed, its overlay deleted, and the journal has no remaining
owned resources. The initial VM and immutable backing image remain available.

The source-level regression suite also covers the stale-lease ordering issue
exposed by the repeat run. Both detailed and pulled-compatible plotting formats
are generated from the final run. `submission.tar.gz` is a draft package; regenerate
it with the actual group identifier before uploading to Eduserver.
