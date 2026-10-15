"""Retain the bounded output-vector slice's acceptance and diagnostic evidence."""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from record_support import digest, read_receipts


def record(work):
    if (work / "acceptance-retry-status").read_text().strip() != "0":
        raise ValueError("acceptance did not finish successfully")
    log = (work / "acceptance-retry.log").read_text()
    match = re.search(r"^Acceptance receipts: (.+)$", log, re.M)
    if not match or "acceptance: PASS" not in log:
        raise ValueError("missing completed acceptance")
    logged_receipt = Path(match.group(1))
    if not logged_receipt.name.startswith("verify.") or logged_receipt.parts[-4:-1] != ("byte", "cgrep", "build"):
        raise ValueError("unexpected receipt path")
    receipt = work / "byte/cgrep/build" / logged_receipt.name
    receipts = read_receipts(ROOT, receipt)
    manifest = json.loads((work / "selected-source.json").read_text())
    tested = {name: value for name, value in manifest.items() if name != "tests/benchmark.py"}
    for name, value in tested.items():
        if digest(ROOT / name) != value:
            raise ValueError("source changed after acceptance: " + name)
    accepted = work / "accepted-cgrep"
    if digest(accepted) != digest(work / "byte/cgrep/build/cgrep"):
        raise ValueError("accepted binary was replaced")
    if (work / "local-build-status").read_text().strip() != "0":
        raise ValueError("local build did not pass output checks")
    if (work / "local.text").read_bytes() != (work / "accepted.text").read_bytes():
        raise ValueError("local/accepted executable text differs")
    reports = {}
    for name in ["prototype-probe", "fast-probe", "byte-probe"]:
        report = json.loads((work / (name + ".json")).read_text())
        if report["qualified"] or report["repeats"] != 7 or len(report["cases"]) != 4:
            raise ValueError("unexpected diagnostic classification")
        for variant, expected in report["binaries"].items():
            if digest(work / "timed-binaries" / variant) != expected:
                raise ValueError("timed binary identity changed: " + variant)
        for case in report["cases"]:
            tools = {variant + "-j" + str(jobs) for variant in report["binaries"] for jobs in [1, 8]}
            if set(case["samples"]) != tools or set(case["cpu_samples"]) != tools:
                raise ValueError("missing fixed diagnostic configuration")
            if any(len(case["samples"][tool]) != 7 or len(case["cpu_samples"][tool]) != 7 for tool in tools):
                raise ValueError("missing raw diagnostic samples")
            if case["validations"] != 7 * len(tools):
                raise ValueError("missing full-output validation")
            for tool in tools:
                values = case["samples"][tool] + case["cpu_samples"][tool]
                if any(not math.isfinite(value) or value < 0 for value in values):
                    raise ValueError("invalid diagnostic sample")
            for variant in report["binaries"]:
                if case["name"].startswith("heavy"):
                    profile = case["callgrind"][variant + "-j8"]
                    if not profile["output_validated"]:
                        raise ValueError("instruction profile output not validated")
                    folder = {"prototype-probe": work, "fast-probe": work / "fast-data",
                              "byte-probe": work / "byte-data"}[name]
                    raw = (folder / (case["name"] + "-" + variant + ".callgrind")).read_text()
                    summary = re.search(r"^summary: (\d+)$", raw, re.M)
                    if not summary or int(summary.group(1)) != profile["instructions"]:
                        raise ValueError("instruction total changed")
        reports[name] = report
    logs = {name: (work / name).read_text() for name in [
        "acceptance.log", "acceptance-retry.log", "prototype-narrow.log", "prototype-probe.log",
        "fast-probe.log", "byte-probe.log", "selected-narrow.log", "selected-sensitivity.log",
        "local-build.log", "timed-rebuild.log"]}
    return {"schema": "cgrep.output-batch-followup/0", "recorded_utc": datetime.now(timezone.utc).isoformat(),
            "status": "correctness accepted, unpublished; performance unqualified",
            "baseline_revision": subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "HEAD"], text=True).strip(),
            "baseline_sha256": digest(work / "accepted-count-format"),
            "accepted_sha256": digest(accepted), "local_executable_sha256": digest(ROOT / "build/cgrep"),
            "local_accepted_text_sha256": digest(work / "accepted.text"),
            "dependency_revision": "75748c7f86dc50c8c16bf4cdf143534508bbba9e",
            "tested_sha256": tested, "receipts": receipts, "reports": reports, "logs": logs,
            "first_acceptance_exit": int((work / "acceptance-status").read_text()),
            "first_acceptance_failure": "isolated snapshot omitted .gitea workflow; copied actual workflow and reran all gates",
            "timed_binary_recovery": "recreated pre-comment byte binary with original source/flags/cwd; whole ELF SHA matches recorded timed identity; accepted source/binary restored afterward",
            "claims": ["borrowed fragments consumed synchronously, at most five per text line",
                       "single callback batch per line; exact bytes and partial failure prefixes tested",
                       "overflow-safe remaining-room preflight; exact fills stay private until existing publication",
                       "fixed 64 KiB channels and all coordinator/flush/ownership/cancellation rules unchanged",
                       "zero-length/null fragments skipped, single-byte stores and oversized-fragment fallback exercised"],
            "limits": ["448 timed full-output diagnostic invocations and 16 full-output Callgrind runs; no quiet timing claim",
                       "selected worker instruction counts fall about 5.3% plain / 7.2% numbered; not native CPU shares or speedups",
                       "native timing results are mixed on the busy host, including higher numbered-output CPU in the last sweep",
                       "all 12,000 ordered differential comparisons and required sanitizers/race/budget checks pass",
                       "bounded portable Cerberus semantics does not interpret pthread execution",
                       "read-only scout review is not an independent concurrency proof",
                       "unrelated benchmark.py and test_benchmark.py excluded from isolated source export, unchanged live",
                       "no new library, handoff pipeline, quiet cloud run, installation, commit/push, or live CI"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("work", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "docs/output-batch-followup.json")
    args = parser.parse_args()
    result = record(args.work.resolve())
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print("PASS retained validated output-batch evidence:", args.output)


if __name__ == "__main__":
    main()
