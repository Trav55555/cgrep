"""Verify a downloaded CI bundle's inventory, hashes and recorded status.

This checks contents, not who uploaded them. --receipts additionally checks
required acceptance results using this checkout's receipt policy.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FILES = {"toolchain-selftest.log", "acceptance.log", "c-utils.json", "cli.json", "cgrep.json"}
MAX_FILE = 32 * 1024 * 1024


def read_file(path):
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_FILE:
        raise ValueError("unsafe or oversized bundle file: " + path.name)
    return path.read_bytes()


def check(bundle):
    if bundle.is_symlink() or not bundle.is_dir():
        raise ValueError("bundle must be a regular directory")
    metadata = json.loads(read_file(bundle / "metadata.json"))
    if not isinstance(metadata, dict) or metadata.get("schema") != "cgrep.ci-artifacts/0":
        raise ValueError("unsupported artifact metadata")
    status = metadata.get("acceptance_exit_code")
    if type(status) is not int or not 0 <= status <= 255 or type(metadata.get("complete_success")) is not bool or metadata["complete_success"] != (status == 0):
        raise ValueError("inconsistent acceptance status")
    revision = metadata.get("revision")
    if revision is not None and (not isinstance(revision, str) or not re.fullmatch("[0-9a-f]{40}", revision)):
        raise ValueError("invalid source revision")
    entries = metadata.get("files")
    if not isinstance(entries, list) or len(entries) > len(FILES):
        raise ValueError("invalid file inventory")
    seen = set()
    for item in entries:
        if not isinstance(item, dict) or not isinstance(item.get("path"), str) or item["path"] not in FILES or item["path"] in seen:
            raise ValueError("unsafe, unknown or duplicate artifact name")
        name = item["path"]
        seen.add(name)
        data = read_file(bundle / name)
        if type(item.get("bytes")) is not int or item["bytes"] != len(data) or item.get("sha256") != hashlib.sha256(data).hexdigest():
            raise ValueError("artifact size or hash mismatch: " + name)
    if set(p.name for p in bundle.iterdir()) != seen | {"metadata.json"}:
        raise ValueError("unlisted bundle contents")
    if status == 0 and seen != FILES:
        raise ValueError("successful bundle missing required evidence")
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bundle", type=Path)
    parser.add_argument("--receipts", action="store_true")
    args = parser.parse_args()
    try:
        metadata = check(args.bundle)
        if args.receipts:
            if metadata["acceptance_exit_code"] != 0:
                raise ValueError("recorded acceptance failed; bundle integrity does not imply PASS")
            for name, tier in [("c-utils", "gate"), ("cli", "fast"), ("cgrep", "deep")]:
                subprocess.run(["python3", "-I", str(ROOT / "tests/check_receipt.py"), str(args.bundle / (name + ".json")), tier], check=True)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        raise SystemExit("invalid artifact bundle: " + str(error))
    print("Bundle integrity: PASS; recorded acceptance exit", metadata["acceptance_exit_code"])


if __name__ == "__main__":
    main()
