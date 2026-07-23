"""Summarise the full-vs-fast timing benchmark.

Reads the CSV written by run_benchmark.sh and prints, per mode, the mean +/-
standard deviation, min and max of the per-experiment event-loop wall time and
CPU time, plus the throughput and the fast/full ratio. Uses only the standard
library, so it runs under any python3. If matplotlib is importable it also
writes a box/scatter plot of the per-experiment times next to the CSV.

    python3 summarise.py [results/timings.csv]
"""

from __future__ import annotations

import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def load(path):
    rows = defaultdict(list)
    with open(path, newline="") as f:
        for row in csv.DictReader(f):
            rows[row["mode"]].append(
                {
                    "events": int(row["events"]),
                    "real": float(row["realElapsed"]),
                    "user": float(row["userCPU"]),
                    "sys": float(row["sysCPU"]),
                }
            )
    return rows


def stats(values):
    n = len(values)
    mean = statistics.fmean(values)
    sd = statistics.stdev(values) if n > 1 else 0.0
    return mean, sd, min(values), max(values)


def summarise(rows):
    order = [m for m in ("full", "fast") if m in rows] + [
        m for m in rows if m not in ("full", "fast")
    ]
    means = {}
    print(f"\n{'mode':6} {'N':>4} {'events':>10} "
          f"{'wall mean+/-sd (s)':>22} {'wall min/max (s)':>18} {'evt/s':>10}")
    print("-" * 78)
    for mode in order:
        data = rows[mode]
        real = [d["real"] for d in data]
        events = data[0]["events"]
        mean, sd, lo, hi = stats(real)
        means[mode] = mean
        rate = events / mean if mean > 0 else float("nan")
        print(f"{mode:6} {len(data):>4} {events:>10} "
              f"{mean:>10.3f} +/- {sd:<7.3f} {lo:>8.3f}/{hi:<8.3f} {rate:>10.0f}")

    # CPU breakdown
    print(f"\n{'mode':6} {'user mean (s)':>14} {'sys mean (s)':>14}")
    print("-" * 38)
    for mode in order:
        data = rows[mode]
        print(f"{mode:6} {statistics.fmean(d['user'] for d in data):>14.3f} "
              f"{statistics.fmean(d['sys'] for d in data):>14.3f}")

    if "full" in means and "fast" in means and means["full"] > 0:
        ratio = means["fast"] / means["full"]
        faster = "faster" if ratio < 1 else "slower"
        print(f"\nfast / full wall-time ratio: {ratio:.2f}x  "
              f"(fast is {faster} than full)")
    return order


def plot(rows, order, out_path):
    try:
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("\n(matplotlib not available -- skipping the plot)")
        return

    fig, ax = plt.subplots(figsize=(7, 4.5))
    data = [[d["real"] for d in rows[m]] for m in order]
    ax.boxplot(data, tick_labels=order, showfliers=False)
    for i, series in enumerate(data, start=1):
        xs = [i + (j / max(len(series) - 1, 1) - 0.5) * 0.3 for j in range(len(series))]
        ax.plot(xs, series, ".", alpha=0.4, color="tab:blue")
    ax.set_ylabel("event-loop wall time per experiment (s)")
    ax.set_title("gamma -> e+e- in Si: full vs flow fast-sim")
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_path, dpi=120)
    print(f"\nwrote {out_path}")


def main():
    csv_path = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("results/timings.csv")
    if not csv_path.exists():
        sys.exit(f"no such file: {csv_path}  (run run_benchmark.sh first)")
    rows = load(csv_path)
    if not rows:
        sys.exit(f"{csv_path} has no data rows")
    order = summarise(rows)
    plot(rows, order, csv_path.with_name("timings.png"))


if __name__ == "__main__":
    main()
