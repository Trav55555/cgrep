"""Acceptance cannot pass with missing checks or a silently skipped tool."""
import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("check_receipt", Path(__file__).with_name("check_receipt.py"))
receipt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(receipt)


class ReceiptTests(unittest.TestCase):
    def setUp(self):
        self.valid = {
            "schema": "dcheck.result/0", "tier": "deep", "status": "pass", "failed": [],
            "checks": [{"name": name, "status": "skip" if name in receipt.OPTIONAL else "pass"}
                       for name in sorted(receipt.EXPECTED["deep"])],
        }

    def test_explicit_optional_skips(self):
        receipt.validate(self.valid, "deep")

    def test_missing_required_check(self):
        broken = copy.deepcopy(self.valid)
        broken["checks"] = [c for c in broken["checks"] if c["name"] != "fuzz"]
        with self.assertRaises(ValueError):
            receipt.validate(broken, "deep")

    def test_skipped_required_check(self):
        broken = copy.deepcopy(self.valid)
        for check in broken["checks"]:
            if check["name"] == "semantics":
                check["status"] = "skip"
        with self.assertRaises(ValueError):
            receipt.validate(broken, "deep")

    def test_failed_check(self):
        broken = copy.deepcopy(self.valid)
        broken["checks"][0]["status"] = "fail"
        with self.assertRaises(ValueError):
            receipt.validate(broken, "deep")

    def test_duplicate_check(self):
        broken = copy.deepcopy(self.valid)
        broken["checks"].append(broken["checks"][0])
        with self.assertRaises(ValueError):
            receipt.validate(broken, "deep")

    def test_schema(self):
        with self.assertRaises(ValueError):
            receipt.validate({}, "deep")


if __name__ == "__main__":
    unittest.main()
