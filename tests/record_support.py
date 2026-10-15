"""Shared file hashes and required-receipt checks for evidence recorders."""
import hashlib
import json
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_receipts(root, directory):
    receipts = {}
    for name, tier in [("c-utils", "gate"), ("cli", "fast"), ("cgrep", "deep")]:
        path = directory / (name + ".json")
        subprocess.run(["python3", "-I", str(root / "tests/check_receipt.py"), str(path), tier], check=True)
        receipts[name] = json.loads(path.read_text())
    return receipts
