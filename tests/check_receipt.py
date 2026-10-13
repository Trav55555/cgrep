"""Reject missing required dcheck checks, failures, and unexpected skips."""
import argparse
import json
from pathlib import Path

GATE = {"subset", "lint", "matrix", "analyzer", "sanitize", "msan", "release",
        "wasm", "coverage", "authority", "regions"}
EXPECTED = {"fast": {"subset", "lint"}, "gate": GATE,
            "deep": GATE | {"semantics", "valgrind", "fuzz", "cbmc"}}
OPTIONAL = {"authority", "regions", "cbmc", "matrix:tcc"}


def validate(result, tier):
    if result.get("schema") != "dcheck.result/0" or result.get("tier") != tier:
        raise ValueError("invalid dcheck receipt schema or tier")
    checks = result.get("checks", [])
    names = [c["name"] for c in checks]
    if len(names) != len(set(names)) or not EXPECTED[tier].issubset(names):
        raise ValueError("duplicate or missing required check in receipt")
    for check in checks:
        status = check.get("status")
        if status == "skip" and check["name"] not in OPTIONAL:
            raise ValueError(f"required check skipped: {check['name']}")
        if status not in {"pass", "skip"}:
            raise ValueError(f"check failed: {check['name']}")
    if result.get("status") != "pass" or result.get("failed") != []:
        raise ValueError("dcheck reported failure")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path)
    parser.add_argument("tier", choices=EXPECTED)
    args = parser.parse_args()
    result = json.loads(args.receipt.read_text())
    for check in result.get("checks", []):
        print(f"{check['status'].upper()} {check['name']} {check.get('detail', '')}".rstrip())
    validate(result, args.tier)


if __name__ == "__main__":
    main()
