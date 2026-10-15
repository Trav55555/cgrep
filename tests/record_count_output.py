"""Retain the count/filename slice's real receipts, sources and raw measurements."""
import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from record_support import digest, read_receipts


def record(work):
    if (work / "acceptance-status").read_text().strip() != "0":
        raise ValueError("acceptance did not finish successfully")
    match = re.search(r"^Acceptance receipts: (.+)$", (work / "acceptance.log").read_text(), re.M)
    if not match:
        raise ValueError("missing acceptance receipt path")
    receipt = Path(match.group(1))
    if receipt.resolve().parent != (work / "acceptance/cgrep/build").resolve():
        raise ValueError("unexpected receipt path")
    receipts = read_receipts(ROOT, receipt)
    snapshot = json.loads((work / "snapshot.json").read_text())
    tested = {name: value for name, value in snapshot.items() if
              name.startswith(("src/", "include/", "cmd/", "semantics/", "tests/")) and name != "tests/benchmark.py"}
    for name, value in tested.items():
        if digest(ROOT / name) != value:
            raise ValueError("source changed after acceptance: " + name)
    candidate = work / "acceptance/cgrep/build/cgrep"
    reports = {name: json.loads((work / (name + ".json")).read_text()) for name in
               ["paired-host", "diagnostic-host", "slices-plain", "slices-numbered", "recheck"]}
    for name in ["slices-plain", "slices-numbered"]:
        if reports[name]["cgrep_sha256"] != digest(candidate):
            raise ValueError("wrong timed candidate identity")
        if reports[name]["baseline_sha256"] != digest(work / "baseline-cgrep"):
            raise ValueError("wrong timed baseline identity")
    instructions = {}
    for label in ["dense", "heavy"]:
        instructions[label] = {}
        for variant in ["baseline", "candidate"]:
            log = (work / f"{label}-{variant}-profile.log").read_text()
            match = re.search(r"I\s+refs:\s+([\d,]+)", log)
            if not match:
                raise ValueError("missing instruction counter")
            instructions[label][variant] = int(match.group(1).replace(",", ""))
    logs = {name: (work / name).read_text() for name in
            ["acceptance.log", "narrow.log", "recheck.log", "profiles.log", "local-build.log"]}
    return {"schema": "cgrep.count-output-followup/0", "recorded_utc": datetime.now(timezone.utc).isoformat(),
            "baseline_revision": subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip(),
            "status": "unpublished working-tree candidate; quiet performance qualification failed",
            "candidate_sha256": digest(candidate), "baseline_sha256": digest(work / "baseline-cgrep"),
            "local_executable_sha256": digest(ROOT / "build/cgrep"),
            "dependency_revision": "75748c7f86dc50c8c16bf4cdf143534508bbba9e",
            "tested_sha256": tested, "receipts": receipts, "reports": reports, "instruction_counts": instructions,
            "logs": logs,
            "limits": ["native validation and bounded semantics are not formal proof",
                       "Cerberus does not interpret native pthread execution",
                       "instruction counts are not native CPU-time shares",
                       "contended wall/CPU samples do not qualify a stable speedup",
                       "no install, commit, push, or fresh live CI"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("work", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "docs/count-output-followup.json")
    args = parser.parse_args()
    result = record(args.work.resolve())
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print("PASS retained validated count/output evidence:", args.output)


if __name__ == "__main__":
    main()
