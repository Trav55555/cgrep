"""Combine retained performance samples with validated acceptance/source identity."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ["src/cgrep.c", "src/parallel.inc", "cmd/main.c", "semantics/sem_find.c"]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--acceptance", type=Path, required=True, help="CI scratch directory containing cgrep and acceptance.log")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    stage = args.acceptance / "cgrep"
    for name in SOURCES:
        if digest(ROOT / name) != digest(stage / name):
            raise RuntimeError("accepted source no longer matches: " + name)
    log = (args.acceptance / "acceptance.log").read_text()
    required = ["acceptance: PASS (", "PASS race detector rejects compiled unsynchronized-write control: 3/3",
                "PASS compiled mutant rejected natively and by Cerberus: lane-batch-overflow"]
    if any(marker not in log for marker in required) or log.count("difftest: 3000/3000 cases identical") != 4:
        raise RuntimeError("acceptance log incomplete")
    receipt = Path(re.search(r"^Acceptance receipts: (.*)$", log, re.M).group(1))
    receipts = {}
    for name, tier in [("c-utils", "gate"), ("cli", "fast"), ("cgrep", "deep")]:
        path = receipt / (name + ".json")
        subprocess.run(["python3", "-I", str(ROOT / "tests/check_receipt.py"), str(path), tier], check=True)
        receipts[name] = json.loads(path.read_text())
    if "selftest: PASS" not in (args.acceptance / "toolchain-selftest.log").read_text():
        raise RuntimeError("missing toolchain selftest")
    binary = digest(stage / "build/cgrep")
    if digest(args.evidence / "accepted-cgrep") != binary:
        raise RuntimeError("measured binary differs from accepted executable")
    reports = {}
    for name in ["final-plain", "final-numbered", "regression-numbered"] + [
            f"{family}-{mode}-{profile}" for family in ["realistic", "repository"]
            for mode in ["warm-file", "warm-pipe", "advise-cold-file"] for profile in ["plain", "numbered"]]:
        report = json.loads((args.evidence / (name + ".json")).read_text())
        if report["cgrep_sha256"] != binary or report["baseline_sha256"] != digest(args.evidence / "baseline-cgrep"):
            raise RuntimeError("benchmark identity mismatch: " + name)
        reports[name] = report
    trace_names = [f"{kind}{suffix}-j{jobs}.strace" for kind in ["heavy", "small"]
                   for suffix in ["", "-accepted"] for jobs in [1, 8]]
    report = {"schema": "cgrep.performance-followup/0", "recorded_utc": datetime.now(timezone.utc).isoformat(),
              "executable_sha256": binary, "source_sha256": {name: digest(ROOT / name) for name in SOURCES},
              "baseline_sha256": digest(args.evidence / "baseline-cgrep"),
              "dependencies": json.loads((ROOT / "ci/dependencies.json").read_text()),
              "acceptance_receipts": receipts, "acceptance_log": log,
              "toolchain_selftest": "PASS", "ci_scope": "local pinned-Gitea-source entry-point pilot, not a server-triggered CI run",
              "ci_wrapper_limit": "outer tool call timed out; full acceptance log subsequently reached PASS; no wrapper exit-status claim",
              "limits": ["contended workstation; wall medians are not stable-host performance guarantees",
                         "serial default retained; overall strongest-competitor target unmet",
                         "targeted cache advice is not proof of physically cold I/O",
                         "repository content copied to explicit ASCII paths; no tool traversal or original-path-cost claim",
                         "independent concurrency review unavailable; bounded semantics excludes pthreads"],
              "traces": {name: (args.evidence / name).read_text() for name in trace_names},
              "post_acceptance_checks": {name: (args.evidence / name).read_text()
                                         for name in ["final-ci-checks.log", "final-framing.log"]},
              "ci_source_sha256": {name: digest(ROOT / name) for name in
                                   [".gitea/workflows/ci.yml", "ci/verify.sh", "ci/fetch_dependencies.py"]},
              "benchmarks": reports}
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print("PASS matched source, executable, acceptance receipts, and all retained reports:", args.output)


if __name__ == "__main__":
    main()
