"""Keep only CI logs, selected dcheck receipts, and non-secret provenance.

Never archives the workspace, dependency source, home directory, or credentials.
Partial/failing receipts are useful evidence and remain marked by the exit code.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import shutil

LOGS = ["toolchain-selftest.log", "acceptance.log"]
RECEIPTS = ["c-utils.json", "cli.json", "cgrep.json"]
MAX_FILE = 32 * 1024 * 1024


def copy_file(source, destination):
    if source.is_symlink() or not source.is_file() or source.stat().st_size > MAX_FILE:
        raise ValueError("unsafe or oversized artifact: " + source.name)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    return {"path": str(destination.name), "bytes": destination.stat().st_size,
            "sha256": hashlib.sha256(destination.read_bytes()).hexdigest()}


def collect(work, output, status, revision):
    if not 0 <= status <= 255 or (revision and not re.fullmatch("[0-9a-f]{40}", revision)):
        raise ValueError("invalid status or revision")
    if output.exists():
        raise ValueError("artifact destination must be fresh")
    output.mkdir(parents=True)
    copied = []
    for name in LOGS:
        source = work / name
        if source.exists():
            copied.append(copy_file(source, output / name))
    receipt = None
    acceptance = work / "acceptance.log"
    if acceptance.is_file():
        if acceptance.is_symlink() or acceptance.stat().st_size > MAX_FILE:
            raise ValueError("unsafe acceptance log")
        match = re.search(r"^Acceptance receipts: (.+)$", acceptance.read_text(), re.M)
        if match:
            receipt = Path(match.group(1))
            expected = work.resolve() / "cgrep/build"
            if expected.resolve() != expected or receipt.is_symlink() or receipt.resolve().parent != expected or not re.fullmatch("verify\\.[A-Za-z0-9]+", receipt.name):
                raise ValueError("receipt path outside generated build")
            for name in RECEIPTS:
                source = receipt / name
                if source.exists():
                    copied.append(copy_file(source, output / name))
    if status == 0 and (receipt is None or set(item["path"] for item in copied) != set(LOGS + RECEIPTS)):
        raise ValueError("successful CI run missing required artifact evidence")
    metadata = {"schema": "cgrep.ci-artifacts/0", "recorded_utc": datetime.now(timezone.utc).isoformat(),
                "revision": revision or None, "acceptance_exit_code": status,
                "complete_success": status == 0, "files": copied}
    (output / "metadata.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print("CI artifact bundle:", output, "exit", status, "files", len(copied), flush=True)
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("work", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--status", type=int, required=True)
    parser.add_argument("--revision", default="")
    args = parser.parse_args()
    collect(args.work, args.output, args.status, args.revision)


if __name__ == "__main__":
    main()
