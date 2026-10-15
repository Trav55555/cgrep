"""Admit and label benchmarks using measured background CPU interference.

Run inside a dedicated 3 GiB/no-swap user scope. CPU usage of that whole
scope is subtracted from /proc/stat, so a worker sweep cannot fail its own
quietness threshold. All completed timings remain retained, eligible or not.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import subprocess
import time


def cpu_ticks(text):
    fields = text.splitlines()[0].split()
    if fields[0] != "cpu" or len(fields) < 9:
        raise ValueError("unsupported aggregate CPU counters")
    values = list(map(int, fields[1:9]))
    return {"total": sum(values), "busy": sum(values[i] for i in [0, 1, 2, 5, 6, 7]), "iowait": values[4]}


def interference(before, after, ticks):
    total = (after["cpu"]["total"] - before["cpu"]["total"]) / ticks
    busy = (after["cpu"]["busy"] - before["cpu"]["busy"]) / ticks
    own = (after["usage_usec"] - before["usage_usec"]) / 1000000
    if total <= 0 or own < 0 or busy < 0:
        raise ValueError("non-monotonic CPU counters")
    return {"background_percent": 100 * max(0, busy - own) / total,
            "scope_cpu_seconds": own, "iowait_percent": 100 * (after["cpu"]["iowait"] - before["cpu"]["iowait"]) / ticks / total,
            "load_per_cpu": after["load"][0] / after["cpus"]}


def quiet(sample, max_background, max_load=None):
    # Load cannot be attributed to a scope. Check it only before admission;
    # during timing it includes our own parallel benchmark workers.
    return sample["background_percent"] <= max_background and sample["iowait_percent"] <= 2 and (max_load is None or sample["load_per_cpu"] <= max_load)


def cgroup():
    row = next(line for line in Path("/proc/self/cgroup").read_text().splitlines() if line.startswith("0::"))
    root = Path("/sys/fs/cgroup") / row[3:].lstrip("/")
    if (root / "memory.max").read_text().strip() != str(3 * 1024 ** 3) or (root / "memory.swap.max").read_text().strip() != "0":
        raise RuntimeError("dedicated 3 GiB/no-swap scope required; no uncapped fallback")
    return root


def snapshot(group):
    usage = dict(line.split() for line in (group / "cpu.stat").read_text().splitlines())
    return {"time": time.monotonic(), "cpu": cpu_ticks(Path("/proc/stat").read_text()),
            "usage_usec": int(usage["usage_usec"]), "load": os.getloadavg(), "cpus": os.cpu_count()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--max-background", type=float, default=10)
    parser.add_argument("--max-load-per-cpu", type=float, default=0.5)
    parser.add_argument("--quiet-seconds", type=int, default=5)
    parser.add_argument("--wait-seconds", type=int, default=120)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command or args.quiet_seconds < 1 or args.wait_seconds < args.quiet_seconds or not 0 <= args.max_background <= 100:
        parser.error("command and valid positive quietness limits required")
    group = cgroup()
    report = {"schema": "cgrep.quiet-benchmark/0", "started_utc": datetime.now(timezone.utc).isoformat(),
              "command": command, "cgroup": str(group), "max_background_percent": args.max_background,
              "max_load_per_cpu": args.max_load_per_cpu, "load_gate": "admission only; during-run load includes benchmark workers",
              "quiet_seconds_required": args.quiet_seconds,
              "limits": "CPU/load admission and during-run monitoring, not isolation or frequency/thermal control",
              "admission": [], "during": [], "admitted": False, "quiet_eligible": False, "returncode": None}
    ticks = os.sysconf("SC_CLK_TCK")
    previous = snapshot(group)
    deadline = time.monotonic() + args.wait_seconds
    consecutive = 0
    while time.monotonic() < deadline:
        time.sleep(1)
        current = snapshot(group)
        sample = interference(previous, current, ticks)
        report["admission"].append(sample)
        consecutive = consecutive + 1 if quiet(sample, args.max_background, args.max_load_per_cpu) else 0
        previous = current
        if consecutive >= args.quiet_seconds:
            report["admitted"] = True
            break
    if report["admitted"]:
        process = subprocess.Popen(command)
        try:
            while process.poll() is None:
                time.sleep(1)
                current = snapshot(group)
                report["during"].append(interference(previous, current, ticks))
                previous = current
            report["returncode"] = process.wait()
            report["quiet_eligible"] = bool(report["during"]) and all(quiet(s, args.max_background) for s in report["during"])
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait()
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print("Quiet benchmark:", "eligible" if report["quiet_eligible"] else "not eligible", args.report, flush=True)
    if not report["admitted"]:
        raise SystemExit(2)
    raise SystemExit(report["returncode"] or (0 if report["quiet_eligible"] else 3))


if __name__ == "__main__":
    main()
