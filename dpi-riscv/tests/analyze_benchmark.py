#!/usr/bin/env python3
"""
analyze_benchmark.py — Compare Herve ISS vs Spike benchmark results.

This script reads CSV output from both the Herve benchmark runner and
the Spike benchmark runner, then produces a side-by-side comparison.

Usage:
  # Run both benchmarks and capture CSV output:
  ./rv32_dpi_benchmark --csv > herve.csv
  ./tests/run_spike_benchmark.sh --csv > spike.csv

  # Compare:
  python3 tests/analyze_benchmark.py herve.csv spike.csv

  # Or run all three in sequence:
  python3 tests/analyze_benchmark.py
    (runs both benchmarks automatically if CSV files don't exist)
"""

import csv
import os
import subprocess
import sys
import math
from collections import OrderedDict


def run_herve_benchmark(csv_path):
    """Run the Herve benchmark and save CSV output."""
    print(f"Running Herve benchmark... ", end="", flush=True)
    herve_bin = os.path.join(os.path.dirname(__file__), "..", "rv32_dpi_benchmark")
    if not os.path.exists(herve_bin):
        # Try building it
        src_dir = os.path.join(os.path.dirname(__file__), "..", "sim", "iss")
        subprocess.run(
            ["g++", "-std=c++11", "-I.", "-O2", "-o", herve_bin,
             os.path.join(src_dir, "rv32_dpi_benchmark.cpp"),
             os.path.join(src_dir, "rv32_dpi.c")],
            cwd=os.path.join(os.path.dirname(__file__), ".."),
            check=True, capture_output=True
        )
    with open(csv_path, "w") as f:
        subprocess.run([herve_bin, "--csv"], cwd=os.path.join(os.path.dirname(__file__), ".."),
                       stdout=f, check=True)
    print("done.")


def run_spike_benchmark(csv_path):
    """Run the Spike benchmark and save CSV output."""
    print(f"Running Spike benchmark... ", end="", flush=True)
    spike_script = os.path.join(os.path.dirname(__file__), "run_spike_benchmark.sh")
    with open(csv_path, "w") as f:
        subprocess.run([spike_script, "--csv"], stdout=f, check=True)
    print("done.")


def load_csv(csv_path):
    """Load benchmark CSV into a dict keyed by test name."""
    results = {}
    with open(csv_path, "r") as f:
        reader = csv.DictReader(f)
        for row in reader:
            name = row["test_name"].strip()
            if name == "TOTAL":
                continue
            results[name] = {
                "passed": row["passed"].strip() == "PASS",
                "instructions": int(row["instructions"]),
                "time_sec": float(row["time_sec"]),
                "ips": float(row["ips"]) if row["ips"] else 0,
                "reason": row.get("reason", "").strip(),
            }
    return results


def main():
    # Determine paths
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_dir = os.path.join(script_dir, "..")
    default_herve_csv = os.path.join(project_dir, "herve_benchmark.csv")
    default_spike_csv = os.path.join(project_dir, "spike_benchmark.csv")

    # Parse arguments
    if len(sys.argv) >= 3:
        herve_csv = sys.argv[1]
        spike_csv = sys.argv[2]
    elif len(sys.argv) == 2:
        herve_csv = sys.argv[1]
        spike_csv = default_spike_csv
        if not os.path.exists(spike_csv):
            run_spike_benchmark(spike_csv)
    else:
        herve_csv = default_herve_csv
        spike_csv = default_spike_csv
        if not os.path.exists(herve_csv):
            run_herve_benchmark(herve_csv)
        if not os.path.exists(spike_csv):
            run_spike_benchmark(spike_csv)

    # Load results
    if not os.path.exists(herve_csv):
        print(f"ERROR: Herve CSV not found: {herve_csv}", file=sys.stderr)
        sys.exit(1)
    if not os.path.exists(spike_csv):
        print(f"ERROR: Spike CSV not found: {spike_csv}", file=sys.stderr)
        sys.exit(1)

    herve_results = load_csv(herve_csv)
    spike_results = load_csv(spike_csv)

    # Find common tests
    all_tests = sorted(set(herve_results.keys()) & set(spike_results.keys()))

    if not all_tests:
        print("ERROR: No common tests found between the two CSV files.", file=sys.stderr)
        print(f"  Herve tests: {len(herve_results)}", file=sys.stderr)
        print(f"  Spike tests: {len(spike_results)}", file=sys.stderr)
        sys.exit(1)

    # Print comparison table
    print("=" * 120)
    print(f"{'Test Name':<40} {'Herve Insn':<12} {'Spike Insn':<12} {'Herve Time':<12} {'Spike Time':<12} {'Herve IPS':<14} {'Spike IPS':<14} {'Speedup':<10}")
    print("=" * 120)

    herve_total_time = 0
    spike_total_time = 0
    herve_total_insn = 0
    spike_total_insn = 0
    speedups = []

    for test_name in all_tests:
        h = herve_results[test_name]
        s = spike_results[test_name]

        herve_total_time += h["time_sec"]
        spike_total_time += s["time_sec"]
        herve_total_insn += h["instructions"]
        spike_total_insn += s["instructions"]

        speedup = s["time_sec"] / h["time_sec"] if h["time_sec"] > 0 else float("inf")
        speedups.append(speedup)

        h_ips_str = f"{h['ips']:,.0f}" if h["ips"] > 0 else "N/A"
        s_ips_str = f"{s['ips']:,.0f}" if s["ips"] > 0 else "N/A"

        print(f"{test_name:<40} {h['instructions']:<12} {s['instructions']:<12} "
              f"{h['time_sec']:<12.6f} {s['time_sec']:<12.6f} "
              f"{h_ips_str:<14} {s_ips_str:<14} {speedup:<10.2f}x")

    # Summary
    print("=" * 120)
    herve_total_ips = herve_total_insn / herve_total_time if herve_total_time > 0 else 0
    spike_total_ips = spike_total_insn / spike_total_time if spike_total_time > 0 else 0
    overall_speedup = spike_total_time / herve_total_time if herve_total_time > 0 else float("inf")

    # Geometric mean of speedups
    if speedups:
        log_sum = sum(math.log(s) for s in speedups if s > 0)
        geo_mean_speedup = math.exp(log_sum / len(speedups))
    else:
        geo_mean_speedup = 0

    print(f"\n{'SUMMARY':<40} {'='*80}")
    print(f"{'Total instructions (Herve):':<40} {herve_total_insn:<12,}")
    print(f"{'Total instructions (Spike):':<40} {spike_total_insn:<12,}")
    print(f"{'Total time (Herve):':<40} {herve_total_time:<12.6f} s")
    print(f"{'Total time (Spike):':<40} {spike_total_time:<12.6f} s")
    print(f"{'Overall IPS (Herve):':<40} {herve_total_ips:<12,.0f}")
    print(f"{'Overall IPS (Spike):':<40} {spike_total_ips:<12,.0f}")
    print(f"{'Overall speedup (Spike/Herve):':<40} {overall_speedup:<10.2f}x")
    print(f"{'Geometric mean speedup:':<40} {geo_mean_speedup:<10.2f}x")
    print(f"{'Min speedup:':<40} {min(speedups):<10.2f}x" if speedups else "")
    print(f"{'Max speedup:':<40} {max(speedups):<10.2f}x" if speedups else "")
    print(f"{'Tests compared:':<40} {len(all_tests)}")
    print()


if __name__ == "__main__":
    main()
