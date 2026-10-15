"""Recorder checks reject altered evidence; fixtures are not real acceptance runs."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

TESTS = Path(__file__).resolve().parent
RECORDERS = {}
for name in ["followup", "count_output", "output_batch"]:
    spec = importlib.util.spec_from_file_location(name, TESTS / ("record_" + name + ".py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    RECORDERS[name] = module


class RecorderTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cgrep-recorder-fixture-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name) / "project"
        self.work = Path(temporary.name) / "evidence"
        self.root.mkdir()
        self.work.mkdir()
        self.write(self.root, "tests/check_receipt.py", (TESTS / "check_receipt.py").read_bytes())
        sources = ["src/cgrep.c", "src/parallel.inc", "cmd/main.c", "semantics/sem_find.c"]
        manifest = {}
        for name in sources:
            data = ("fixture source: " + name + "\n").encode()
            self.write(self.root, name, data)
            self.write(self.work, "acceptance/cgrep/" + name, data)
            manifest[name] = hashlib.sha256(data).hexdigest()
        manifest["tests/benchmark.py"] = "excluded unrelated source"
        self.json("selected-source.json", manifest)
        self.json("snapshot.json", manifest)
        self.write(self.root, "build/cgrep", b"local build fixture\n")
        for name in ["accepted-cgrep", "byte/cgrep/build/cgrep", "acceptance/cgrep/build/cgrep"]:
            self.write(self.work, name, b"accepted build fixture\n")
        self.write(self.work, "accepted-count-format", b"counting predecessor fixture\n")
        self.write(self.work, "baseline-cgrep", b"installed baseline fixture\n")
        self.write(self.work, "local.text", b"matching executable text\n")
        self.write(self.work, "accepted.text", b"matching executable text\n")
        for name in ["acceptance-retry-status", "local-build-status", "acceptance-status"]:
            self.write(self.work, name, b"0\n")
        self.receipt = self.work / "byte/cgrep/build/verify.fixture"
        self.count_receipt = self.work / "acceptance/cgrep/build/verify.fixture"
        gate = ["subset", "lint", "matrix", "analyzer", "sanitize", "msan", "release",
                "wasm", "coverage", "authority", "regions"]
        for name, tier, checks in [("c-utils", "gate", gate), ("cli", "fast", ["subset", "lint"]),
                                   ("cgrep", "deep", gate + ["semantics", "valgrind", "fuzz", "cbmc"])]:
            receipt = {"schema": "dcheck.result/0", "tier": tier, "status": "pass", "failed": [],
                       "checks": [{"name": check, "status": "skip" if check in ["authority", "regions", "cbmc"] else "pass"}
                                  for check in checks]}
            for folder in [self.receipt, self.count_receipt]:
                self.write(folder, name + ".json", json.dumps(receipt).encode())
        self.write(self.work, "acceptance-retry.log",
                   b"Acceptance receipts: /original/location/byte/cgrep/build/verify.fixture\nacceptance: PASS fixture\n")
        acceptance = ("Acceptance receipts: " + str(self.count_receipt) + "\nacceptance: PASS (fixture)\n"
                      "PASS race detector rejects compiled unsynchronized-write control: 3/3\n"
                      "PASS compiled mutant rejected natively and by Cerberus: lane-batch-overflow\n"
                      + "difftest: 3000/3000 cases identical\n" * 4).encode()
        self.write(self.work, "acceptance.log", acceptance)
        self.write(self.work, "acceptance/acceptance.log", acceptance)
        self.write(self.work, "acceptance/toolchain-selftest.log", b"selftest: PASS fixture\n")
        for name in ["prototype-narrow", "prototype-probe", "fast-probe", "byte-probe", "selected-narrow",
                     "selected-sensitivity", "local-build", "timed-rebuild", "narrow", "recheck", "profiles",
                     "final-ci-checks", "final-framing"]:
            self.write(self.work, name + ".log", b"fixture log\n")
        for name in [".gitea/workflows/ci.yml", "ci/verify.sh", "ci/fetch_dependencies.py"]:
            self.write(self.root, name, b"fixture source\n")
        self.write(self.root, "ci/dependencies.json", b'{"fixture": true}\n')
        for variant in ["baseline", "vector", "fast", "byte"]:
            self.write(self.work, "timed-binaries/" + variant, (variant + " fixture\n").encode())
        for report_name, variants, folder in [
            ("prototype-probe", ["baseline", "vector"], ""),
            ("fast-probe", ["baseline", "vector", "fast"], "fast-data"),
            ("byte-probe", ["baseline", "fast", "byte"], "byte-data"),
        ]:
            binaries = {v: self.digest(self.work / "timed-binaries" / v) for v in variants}
            cases = []
            for case_name in ["heavy-plain", "heavy-numbered", "small-plain", "small-numbered"]:
                tools = [v + "-j" + str(j) for v in variants for j in [1, 8]]
                case = {"name": case_name, "samples": {t: [0.001 * i for i in range(1, 8)] for t in tools},
                        "cpu_samples": {t: [0.002 * i for i in range(1, 8)] for t in tools},
                        "validations": 7 * len(tools)}
                if case_name.startswith("heavy"):
                    case["callgrind"] = {v + "-j8": {"instructions": 12345, "output_validated": True} for v in variants}
                    for variant in variants:
                        self.write(self.work / folder, case_name + "-" + variant + ".callgrind", b"summary: 12345\n")
                cases.append(case)
            self.json(report_name + ".json", {"qualified": False, "repeats": 7, "binaries": binaries, "cases": cases})
        identity = {"cgrep_sha256": self.digest(self.work / "accepted-cgrep"),
                    "baseline_sha256": self.digest(self.work / "baseline-cgrep")}
        for name in ["paired-host", "diagnostic-host", "slices-plain", "slices-numbered", "recheck",
                     "final-plain", "final-numbered", "regression-numbered"] + [
                f"{family}-{mode}-{profile}" for family in ["realistic", "repository"]
                for mode in ["warm-file", "warm-pipe", "advise-cold-file"] for profile in ["plain", "numbered"]]:
            self.json(name + ".json", identity)
        for kind in ["dense", "heavy"]:
            for variant in ["baseline", "candidate"]:
                self.write(self.work, f"{kind}-{variant}-profile.log", b"I refs: 1,234\n")
        for kind in ["heavy", "small"]:
            for suffix in ["", "-accepted"]:
                for jobs in [1, 8]:
                    self.write(self.work, f"{kind}{suffix}-j{jobs}.strace", b"fixture trace\n")

    @staticmethod
    def write(root, name, data):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    @staticmethod
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def json(self, name, data):
        self.write(self.work, name, (json.dumps(data) + "\n").encode())

    def record(self, name="output_batch"):
        module = RECORDERS[name]
        # Git supplies provenance metadata only. Receipt validation remains a real subprocess.
        with patch.object(module, "ROOT", self.root), patch.object(module.subprocess, "check_output", return_value="1" * 40 + "\n"):
            if name != "followup":
                return module.record(self.work)
            output = self.work / "followup-result.json"
            with patch.object(sys, "argv", ["record_followup.py", "--evidence", str(self.work),
                                            "--acceptance", str(self.work / "acceptance"), "--output", str(output)]):
                module.main()
            return json.loads(output.read_text())

    def test_valid_evidence_keeps_reports_identities_and_receipts(self):
        result = self.record()
        self.assertEqual(result["schema"], "cgrep.output-batch-followup/0")
        self.assertEqual(result["accepted_sha256"], self.digest(self.work / "accepted-cgrep"))
        self.assertEqual(result["baseline_sha256"], self.digest(self.work / "accepted-count-format"))
        self.assertEqual(result["local_executable_sha256"], self.digest(self.root / "build/cgrep"))
        for name in ["prototype-probe", "fast-probe", "byte-probe"]:
            self.assertEqual(result["reports"][name], json.loads((self.work / (name + ".json")).read_text()))
        self.assertEqual(set(result["receipts"]), {"c-utils", "cli", "cgrep"})
        self.assertEqual(result["status"], "correctness accepted, unpublished; performance unqualified")

    def test_failed_acceptance_is_rejected(self):
        self.write(self.work, "acceptance-retry-status", b"2\n")
        with self.assertRaisesRegex(ValueError, "acceptance did not finish successfully"):
            self.record()

    def test_failed_cli_does_not_replace_output(self):
        self.write(self.work, "acceptance-retry-status", b"2\n")
        output = self.work / "existing-report.json"
        output.write_bytes(b"keep prior report\n")
        result = subprocess.run(["python3", "-I", str(TESTS / "record_output_batch.py"),
                                 str(self.work), "--output", str(output)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("acceptance did not finish successfully", result.stderr)
        self.assertEqual(output.read_bytes(), b"keep prior report\n")

    def test_source_drift_is_rejected(self):
        self.write(self.root, "src/cgrep.c", b"changed source\n")
        with self.assertRaisesRegex(ValueError, "source changed after acceptance: src/cgrep.c"):
            self.record()

    def test_wrong_timed_identity_is_rejected(self):
        report = json.loads((self.work / "byte-probe.json").read_text())
        report["binaries"]["byte"] = "0" * 64
        self.json("byte-probe.json", report)
        with self.assertRaisesRegex(ValueError, "timed binary identity changed: byte"):
            self.record()

    def test_missing_sample_is_rejected(self):
        report = json.loads((self.work / "byte-probe.json").read_text())
        report["cases"][0]["samples"]["byte-j8"].pop()
        self.json("byte-probe.json", report)
        with self.assertRaisesRegex(ValueError, "missing raw diagnostic samples"):
            self.record()

    def test_nan_timing_is_rejected(self):
        report = json.loads((self.work / "byte-probe.json").read_text())
        report["cases"][0]["cpu_samples"]["byte-j8"][0] = float("nan")
        self.json("byte-probe.json", report)
        with self.assertRaisesRegex(ValueError, "invalid diagnostic sample"):
            self.record()

    def test_altered_instruction_total_is_rejected(self):
        report = json.loads((self.work / "byte-probe.json").read_text())
        report["cases"][0]["callgrind"]["byte-j8"]["instructions"] = 12346
        self.json("byte-probe.json", report)
        with self.assertRaisesRegex(ValueError, "instruction total changed"):
            self.record()

    def test_all_recorders_accept_valid_receipts(self):
        for name in RECORDERS:
            with self.subTest(recorder=name):
                result = self.record(name)
                receipts = result["acceptance_receipts" if name == "followup" else "receipts"]
                self.assertEqual(receipts["cli"]["tier"], "fast")
                self.assertEqual(receipts["c-utils"]["tier"], "gate")
                self.assertEqual(receipts["cgrep"]["tier"], "deep")

    def test_all_recorders_reject_failed_receipts(self):
        for folder in [self.receipt, self.count_receipt]:
            path = folder / "cgrep.json"
            data = json.loads(path.read_text())
            data["status"] = "fail"
            path.write_text(json.dumps(data))
        for name in RECORDERS:
            with self.subTest(recorder=name), self.assertRaises(subprocess.CalledProcessError):
                self.record(name)

    def test_all_recorders_reject_skipped_required_checks(self):
        for folder in [self.receipt, self.count_receipt]:
            path = folder / "cli.json"
            data = json.loads(path.read_text())
            data["checks"][0]["status"] = "skip"
            path.write_text(json.dumps(data))
        for name in RECORDERS:
            with self.subTest(recorder=name), self.assertRaises(subprocess.CalledProcessError):
                self.record(name)


if __name__ == "__main__":
    unittest.main()
