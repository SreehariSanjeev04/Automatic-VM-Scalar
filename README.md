# Cloud AutoScalar

Cloud AutoScalar is a C++20 VM autoscaling prototype built on libvirt and
QEMU/KVM. It monitors the CPU usage of a running VM, routes work to registered
VMs, and creates or removes VM replicas when sustained load crosses configured
thresholds.

The repository builds two programs:

- `cloud_autoscalar`: the orchestrator, load balancer, VM monitor, and scaler.
- `cloud_autoscalar_client`: an interactive traffic-generation CLI.

## Architecture

```text
cloud_autoscalar_client
        |
        | POST /work {"value": N}
        v
Host LoadBalancer :8080
        |
        | GET /binary-trees?n=N
        v
Registered guest VMs :8080
        ^
        |
Autoscaler <-> libvirt qemu:///system
    |       |
    |       +-- VMMonitor reads CPU usage
    +---------- VMCloner creates/removes replicas
```

### Runtime components

1. **Orchestrator**  
   [Orchestrator.cpp](./orchestrator/Orchestrator.cpp) connects to the system
   libvirt instance, initializes the VM cloner and autoscaler, and starts the
   host load balancer.

2. **Autoscaler**  
   [Autoscaler.cpp](./autoscaler/Autoscaler.cpp) expects an initial running
   domain named `vm`. It discovers the VM's DHCP address, waits for its
   application on port `8080`, and registers its address. Sustained CPU usage
   above `75%` triggers scale-out after three observations. When more than one
   replica exists, CPU usage below `25%` for five observations triggers
   scale-in.

3. **VM cloner**  
   New replicas are named `vm-2`, `vm-3`, and so on. They are cloned from the
   libvirt-managed base volume and attached to libvirt's `default` network.
   The configured maximum is currently two replicas.

4. **Load balancer**  
   The host listener binds to `0.0.0.0:8080`. It accepts `POST /work` requests,
   selects registered VMs round-robin, and forwards work to
   `GET /binary-trees?n=<value>`.

5. **Client**  
   The CLI sends requests to `192.168.122.1:8080`. Use `increase` for high
   traffic, `decrease` for low traffic, and `exit` to stop the client.

6. **Guest computation server**  
   [Catalan.cpp](./computation/Catalan.cpp) runs inside each guest VM. It
   exposes `GET /binary-trees?n=<integer>` on `0.0.0.0:8080` and calculates the
   number of binary trees using a deliberately expensive recursive algorithm.
   The server must be compiled and managed by systemd inside the initial VM.
   Provision the service in the base VM image so that every autoscaler replica
   starts the same server.

## Host dependencies

The commands below target Debian or Ubuntu:

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  pkg-config \
  libvirt-dev \
  libvirt-daemon-system \
  libvirt-clients \
  qemu-system-x86 \
  qemu-utils \
  curl
```

Enable libvirt and add the current user to the required groups:

```bash
sudo systemctl enable --now libvirtd
sudo usermod -aG libvirt,kvm "$USER"
```

Log out and back in after changing group membership. Confirm that the system
connection works:

```bash
virsh -c qemu:///system list --all
```

The default libvirt network must be active:

```bash
virsh -c qemu:///system net-start default
virsh -c qemu:///system net-autostart default
```

If the network is already active, `net-start` may report that no action is
needed.

## Storage and VM prerequisites

The current implementation uses these values in
[Orchestrator.cpp](./orchestrator/Orchestrator.cpp) and
[Autoscaler.cpp](./autoscaler/Autoscaler.cpp):

| Item | Required value |
|---|---|
| Libvirt URI | `qemu:///system` |
| Storage pool | `images` |
| Base volume path | `/mnt/LinuxData/images/base.qcow2` |
| Initial VM domain | `vm` |
| Guest application port | `8080` |
| Guest application endpoint | `GET /binary-trees?n=<value>` |
| Host load balancer | `0.0.0.0:8080` |
| Client target | `192.168.122.1:8080` |

These values, along with VM sizing, replica limits, scaling thresholds,
timeouts, and traffic volume, are configured in
[autoscaler.config](./autoscaler.config). The file is JSON and can be copied
and edited for another environment.

The base image must be an existing libvirt-managed volume in the `images`
storage pool. The pool must be active:

```bash
virsh -c qemu:///system pool-info images
virsh -c qemu:///system vol-list images
```

If the pool does not exist, define it against the directory containing the
base image. Do not define a second pool with the same name:

```bash
sudo mkdir -p /mnt/LinuxData/images
sudo virsh pool-define-as images dir --target /mnt/LinuxData/images
sudo virsh pool-start images
sudo virsh pool-autostart images
```

Place the base image at the required path and verify it is visible to libvirt:

```bash
virsh -c qemu:///system vol-info base.qcow2 --pool images
```

The initial VM must already exist and be running:

```bash
virsh -c qemu:///system dominfo vm
virsh -c qemu:///system domifaddr vm --source lease
```

Inside `vm`, build the guest computation server from
[Catalan.cpp](./computation/Catalan.cpp):

```bash
sudo install -d /opt/cloud-autoscalar
sudo install -m 0644 computation/Catalan.cpp /opt/cloud-autoscalar/Catalan.cpp
sudo install -m 0644 commons/httplib.h /opt/cloud-autoscalar/httplib.h
sudo c++ -std=c++20 -O2 -pthread \
  -I/opt/cloud-autoscalar \
  /opt/cloud-autoscalar/Catalan.cpp \
  -o /opt/cloud-autoscalar/catalan-server
```

Create `/etc/systemd/system/catalan-server.service` inside the VM. Replace
`vmuser` with the unprivileged user that should run the server:

```ini
[Unit]
Description=Cloud AutoScalar Catalan computation server
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=vmuser
WorkingDirectory=/opt/cloud-autoscalar
ExecStart=/opt/cloud-autoscalar/catalan-server
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
```

Reload systemd, enable the service at boot, and start it:

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now catalan-server.service
sudo systemctl status catalan-server.service
```

Verify the service before using the VM as the base image:

```bash
curl "http://127.0.0.1:8080/binary-trees?n=20"
sudo journalctl -u catalan-server.service --no-pager
```

The service must be active in the initial VM before the autoscaler starts.
Because replicas are cloned from the base image, repeat this provisioning
after changing the server binary or unit file and recreate any existing
replicas if they should receive the update.

The guest server listens on `0.0.0.0:8080` and implements:

```text
GET /binary-trees?n=<integer>
```

The autoscaler waits up to 60 seconds for both the VM DHCP lease and the guest
application. The orchestrator will not start the host load balancer until this
initial readiness check succeeds.

Check guest readiness from the host before starting the orchestrator:

```bash
curl "http://<VM_IP>:8080/binary-trees?n=20"
```

Replace `<VM_IP>` with the address returned by `domifaddr`.

## Build

From the repository root:

```bash
cmake -S . -B build
cmake --build build --parallel
```

The resulting binaries are:

```text
build/cloud_autoscalar
build/cloud_autoscalar_client
```

## Run

Start the orchestrator first:

```bash
sudo ./build/cloud_autoscalar
```

Use an alternate configuration file with:

```bash
sudo ./build/cloud_autoscalar /path/to/autoscaler.config
```

Use `sudo` only if the current user cannot access the system libvirt socket.
The orchestrator should log the following startup phases:

```text
Connecting to libvirt
Connected to libvirt
Initializing VM cloner
Initializing autoscaler
Initial VM registered: vm
Starting load balancer on port 8080
```

In a second terminal, start the client:

```bash
./build/cloud_autoscalar_client
```

The client also accepts an alternate configuration path:

```bash
./build/cloud_autoscalar_client /path/to/autoscaler.config
```

Available commands:

```text
> increase
> decrease
> exit
```

The current request settings are defined in
[Constant.hpp](./commons/Constant.hpp). The default client configuration sends
one request per command with one worker thread. Increase these values to
generate a longer or heavier workload.

## Verify the host load balancer

The load balancer accepts JSON with an integer `value`:

```bash
curl -i \
  -H 'Content-Type: application/json' \
  -d '{"value":20}' \
  http://192.168.122.1:8080/work
```

A successful request returns a JSON success response. A `500` response usually
means that no VM is registered or that the guest application is unavailable.
Backend connections allow up to 30 seconds for the guest application to
complete a computation; connection establishment itself times out after two
seconds.

## Scaling behavior

The autoscaler checks each active VM every two seconds:

- CPU above `75%` increments that VM's overload counter.
- Three consecutive overload observations trigger scale-out.
- CPU below `25%` increments its low-load counter.
- Five consecutive low-load observations across the active VMs trigger
  scale-in.
- The initial VM named `vm` is never removed.

Scale-out creates a replica, waits for its DHCP address, waits for its guest
application on port `8080`, and registers it with the load balancer. Scale-in
deregisters and removes the most recently created replica.

## Troubleshooting

### No orchestrator output

Run the freshly built binary and confirm the build succeeded:

```bash
cmake --build build --target cloud_autoscalar
sudo ./build/cloud_autoscalar
```

If the last message is `Waiting for DHCP lease`, check the VM state and
libvirt lease:

```bash
virsh -c qemu:///system domstate vm
virsh -c qemu:///system domifaddr vm --source lease
```

If the last message is `Waiting for initial VM server`, the VM is reachable
but its application is not accepting TCP connections on port `8080`.

If the load balancer reports `Failed to read connection`, the guest application
closed the connection or did not respond within the 30-second backend request
timeout. Test the guest endpoint directly and inspect the guest application
logs:

```bash
curl -v "http://<VM_IP>:8080/binary-trees?n=20"
```

### Client cannot connect

Confirm that the orchestrator is listening:

```bash
ss -ltn | grep ':8080'
```

Confirm the host endpoint:

```bash
curl -i http://192.168.122.1:8080/
```

`404 Not Found` for `/` is expected; the load balancer's application endpoint
is `/work`.

### No VM is available

The autoscaler registers the initial VM only after its guest application is
ready. Check the orchestrator logs and verify:

```bash
virsh -c qemu:///system list --all
curl "http://<VM_IP>:8080/binary-trees?n=2"
```

### Build errors

Check that CMake can find libvirt:

```bash
pkg-config --modversion libvirt
```

If this fails, install `libvirt-dev` and rerun configuration:

```bash
cmake -S . -B build
```

## Current limitations

- Runtime paths, VM names, IP addresses, ports, thresholds, and replica limits
  are currently compiled into the source; there is no configuration file or
  environment-variable support yet.
- The initial VM and guest application must be provisioned manually.
- The guest application is not included in this repository.
- The orchestrator currently runs continuously and is stopped with `Ctrl+C`.
- The host load balancer and client communicate over the libvirt default
  network address `192.168.122.1`.
