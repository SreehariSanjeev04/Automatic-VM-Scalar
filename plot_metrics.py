#!/usr/bin/env python3

from pathlib import Path
import sys

import pandas as pd
import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


RESULTS_DIR = Path("results")
PLOTS_DIR = RESULTS_DIR / "plots"
CPU_FILE = RESULTS_DIR / "vm_metrics.csv"
REQUEST_FILE = RESULTS_DIR / "request_metrics.csv"
WINDOW_SECONDS = 5


def load_metrics():
    if not CPU_FILE.exists():
        raise FileNotFoundError(f"Missing {CPU_FILE}")
    if not REQUEST_FILE.exists():
        raise FileNotFoundError(f"Missing {REQUEST_FILE}")

    cpu = pd.read_csv(CPU_FILE)
    requests = pd.read_csv(REQUEST_FILE)

    required_cpu = {"timestamp_ms", "vm_name", "cpu_percent", "replica_count"}
    required_requests = {
        "timestamp_ms", "backend_ip", "latency_ms", "success", "http_status"
    }

    if not required_cpu.issubset(cpu.columns):
        raise ValueError(f"{CPU_FILE} must contain: {sorted(required_cpu)}")
    if not required_requests.issubset(requests.columns):
        raise ValueError(
            f"{REQUEST_FILE} must contain: {sorted(required_requests)}"
        )

    for frame in (cpu, requests):
        frame["timestamp_ms"] = pd.to_numeric(
            frame["timestamp_ms"], errors="coerce"
        )
        frame.dropna(subset=["timestamp_ms"], inplace=True)
        frame["time"] = pd.to_datetime(
            frame["timestamp_ms"], unit="ms", utc=True
        )

    cpu["cpu_percent"] = pd.to_numeric(cpu["cpu_percent"], errors="coerce")
    cpu["replica_count"] = pd.to_numeric(
        cpu["replica_count"], errors="coerce"
    )
    requests["latency_ms"] = pd.to_numeric(
        requests["latency_ms"], errors="coerce"
    )
    requests["success"] = pd.to_numeric(requests["success"], errors="coerce")

    cpu.dropna(subset=["cpu_percent", "replica_count"], inplace=True)
    requests.dropna(subset=["success"], inplace=True)

    if cpu.empty and requests.empty:
        raise ValueError("Both metrics files are empty.")

    return cpu, requests


def experiment_start(cpu, requests):
    timestamps = []
    if not cpu.empty:
        timestamps.append(cpu["timestamp_ms"].min())
    if not requests.empty:
        timestamps.append(requests["timestamp_ms"].min())
    return min(timestamps)


def add_scale_events(ax, replica_series):
    """Mark timestamps where the observed replica count changes."""
    changes = replica_series[
        replica_series["replica_count"].ne(
            replica_series["replica_count"].shift()
        )
    ]

    for _, row in changes.iloc[1:].iterrows():
        count = int(row["replica_count"])
        label = "Scale out" if count > int(
            changes.iloc[list(changes.index).index(row.name) - 1]["replica_count"]
        ) else "Scale in"
        ax.axvline(row["seconds"], linestyle="--", linewidth=1)
        ax.text(
            row["seconds"], 0.98, label,
            transform=ax.get_xaxis_transform(),
            rotation=90, va="top", ha="right"
        )


def make_plots(cpu, requests, start_ms):
    output_dir = PLOTS_DIR
    try:
        output_dir.mkdir(parents=True, exist_ok=True)
    except PermissionError:
        output_dir = Path("plots")
        output_dir.mkdir(parents=True, exist_ok=True)
        print(
            f"Cannot write to {PLOTS_DIR}; writing plots to {output_dir}",
            file=sys.stderr,
        )

    cpu["seconds"] = (cpu["timestamp_ms"] - start_ms) / 1000.0
    requests["seconds"] = (requests["timestamp_ms"] - start_ms) / 1000.0

    if not cpu.empty:
        replica_series = (
            cpu.sort_values("seconds")[["seconds", "replica_count"]]
            .drop_duplicates("replica_count")
        )

        figure, axis = plt.subplots(figsize=(12, 6))
        for vm_name, vm_data in cpu.groupby("vm_name"):
            axis.plot(
                vm_data["seconds"],
                vm_data["cpu_percent"],
                marker=".",
                linewidth=1,
                label=vm_name,
            )
        axis.set_title("VM CPU utilization")
        axis.set_xlabel("Seconds since experiment start")
        axis.set_ylabel("CPU (%)")
        axis.grid(True, alpha=0.3)
        axis.legend()
        add_scale_events(axis, replica_series)
        figure.tight_layout()
        figure.savefig(output_dir / "vm_cpu.png", dpi=150)
        plt.close(figure)

    if not requests.empty:
        request_data = requests.sort_values("seconds").copy()
        request_data["successful"] = request_data["success"].eq(1)

        figure, (latency_axis, throughput_axis) = plt.subplots(
            2, 1, figsize=(12, 9), sharex=True
        )
        latency_axis.plot(
            request_data["seconds"],
            request_data["latency_ms"],
            marker=".",
            linestyle="none",
            label="Request latency",
        )
        latency_axis.set_title("Request latency")
        latency_axis.set_ylabel("Latency (ms)")
        latency_axis.grid(True, alpha=0.3)
        latency_axis.legend()

        request_data = request_data.set_index("time")
        throughput = (
            request_data["successful"]
            .resample(f"{WINDOW_SECONDS}s")
            .sum()
            .fillna(0)
        )
        throughput_seconds = (
            (throughput.index - request_data.index.min()).total_seconds()
        )
        throughput_axis.plot(
            throughput_seconds,
            throughput / WINDOW_SECONDS,
            drawstyle="steps-post",
            label="Successful requests/sec",
        )
        throughput_axis.set_title("Request throughput")
        throughput_axis.set_xlabel("Seconds since experiment start")
        throughput_axis.set_ylabel("Requests/sec")
        throughput_axis.grid(True, alpha=0.3)
        throughput_axis.legend()

        figure.tight_layout()
        figure.savefig(output_dir / "request_metrics.png", dpi=150)
        plt.close(figure)


def main():
    try:
        cpu, requests = load_metrics()
        make_plots(cpu, requests, experiment_start(cpu, requests))
    except (OSError, ValueError, ImportError) as error:
        print(f"Could not create metric plots: {error}", file=sys.stderr)
        return 1

    print(f"Created metric plots in {PLOTS_DIR if PLOTS_DIR.exists() else Path('plots')}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
