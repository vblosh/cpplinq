#!/usr/bin/env python3
import json
import subprocess
import statistics
import os
import sys

def run_benchmarks(num_runs=5):
    all_results = {} # bench_name -> list of real_time_ms

    env = os.environ.copy()
    bin_path = "./build-bench/benchmarks/bench_mysql_perf"

    for run_idx in range(num_runs):
        print(f"--- Running benchmark iteration {run_idx + 1}/{num_runs} ---")
        cmd = [bin_path, "--benchmark_format=json"]
        proc = subprocess.run(cmd, env=env, capture_output=True, text=True)
        if proc.returncode != 0:
            print(f"Error running benchmark: {proc.stderr}", file=sys.stderr)
            sys.exit(1)
        
        data = json.loads(proc.stdout)
        for b in data.get("benchmarks", []):
            name = b["name"]
            # Time in ms (real_time is in ms when time_unit is ms)
            time_ms = b["real_time"]
            if b.get("time_unit") == "ns":
                time_ms = time_ms / 1e6
            elif b.get("time_unit") == "us":
                time_ms = time_ms / 1e3
            elif b.get("time_unit") == "s":
                time_ms = time_ms * 1e3
            
            if name not in all_results:
                all_results[name] = []
            all_results[name].append(time_ms)

    medians = {}
    for name, times in all_results.items():
        med = statistics.median(times)
        medians[name] = med
        print(f"{name:60s}: Median = {med:.2f} ms (runs: {', '.join(f'{t:.2f}' for t in times)})")

    with open("mysql_bench_results.json", "w") as f:
        json.dump({"medians": medians, "raw": all_results}, f, indent=2)

    return medians

if __name__ == "__main__":
    runs = 5
    if len(sys.argv) > 1:
        runs = int(sys.argv[1])
    run_benchmarks(runs)
