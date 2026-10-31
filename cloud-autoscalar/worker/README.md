# Guest worker

This stateless C++ HTTP service runs inside each server VM, listens on port 8080,
and uses the repository's bundled cpp-httplib header. It has eight request threads.

- `GET /health`: readiness check.
- `GET /binary-trees?n=20`: CPU-intensive binary-tree allocation and traversal,
  matching the host load balancer's backend request.
- `GET /work?value=2`: equivalent workload endpoint for direct requests.

Input must be an integer from 0 through 100. Each request allocates and traverses
one complete binary tree of the requested depth. There is no tree-depth cap,
repetition substitution, or service `MemoryMax` setting. Both workload endpoints
accept the same range. Responses report `tree_depth` equal to the input and
`repetitions` equal to 1.

A depth-n tree has `2^(n+1)-1` nodes: CPU work and memory approximately double
with each increment. On a 64-bit guest, each node contains two pointers (16 bytes),
plus allocator overhead. Depth 20 needs roughly 64 MiB including typical allocator
overhead; concurrent requests multiply demand. The guest's physical RAM and swap
still limit what can complete. Large inputs can cause swapping or an OOM kill;
accepting 100 does not make a depth-100 tree feasible. Allocation failures return
HTTP 503 if the process can catch them before the kernel kills it.

Start with modest inputs and monitor guest memory while increasing depth:

```bash
curl --max-time 180 'http://192.168.122.86:8080/binary-trees?n=20'
```

The current host load balancer and client have two-second read timeouts;
use the guest endpoint directly for these longer requests, or adjust those
timeouts before routing larger workloads through the load balancer.

## Install inside an Ubuntu guest

Copy `main.cpp`, `cloud-worker.service`, and `../../commons/httplib.h` to a staging
directory inside the guest, then run the following there:

```bash
sudo apt-get update
sudo apt-get install -y g++
g++ -std=c++20 -O2 -pthread -I. main.cpp -o cloud-worker
sudo install -d -m 755 /opt/cloud-worker
sudo install -m 755 cloud-worker /opt/cloud-worker/cloud-worker
sudo useradd --system --user-group --no-create-home --shell /usr/sbin/nologin cloud-worker
sudo install -m 644 cloud-worker.service /etc/systemd/system/cloud-worker.service
sudo systemctl daemon-reload
sudo systemctl enable --now cloud-worker.service
```

Skip `useradd` if the service account already exists. If UFW is active, allow
TCP 8080 from the host's actual libvirt subnet. No firewall change is needed
when it is inactive.

```bash
systemctl status cloud-worker --no-pager
curl --fail http://127.0.0.1:8080/health
curl --fail 'http://127.0.0.1:8080/binary-trees?n=20'
journalctl -u cloud-worker --no-pager -n 20
```

The enabled service will also start in clones made from this guest's prepared
disk image. Shut down the guest before preparing an immutable backing image.
