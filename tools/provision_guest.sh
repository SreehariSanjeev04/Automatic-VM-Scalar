#!/bin/sh
# Run inside the guest from a staging directory containing the computation sources.
set -eu
source_dir=${1:-.}
command -v g++ >/dev/null || { sudo apt-get update; sudo apt-get install -y g++; }
g++ -std=c++20 -O2 -pthread -I"$source_dir" "$source_dir/Catalan.cpp" -o "$source_dir/catalan-server"
getent passwd cloud-worker >/dev/null || sudo useradd --system --user-group --no-create-home --shell /usr/sbin/nologin cloud-worker
sudo install -d -m 755 /opt/cloud-autoscalar
sudo install -m 755 "$source_dir/catalan-server" /opt/cloud-autoscalar/catalan-server
sudo install -m 644 "$source_dir/catalan-server.service" /etc/systemd/system/catalan-server.service
if systemctl list-unit-files cloud-worker.service --no-legend | grep -q cloud-worker; then sudo systemctl disable --now cloud-worker; fi
sudo systemctl daemon-reload
sudo systemctl enable --now catalan-server
curl --fail --retry 3 --retry-connrefused http://127.0.0.1:8080/health
