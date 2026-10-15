"""Allow-list, tamper, failure-status, and runner-cleanup artifact sensors."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("artifacts", ROOT / "ci/collect_artifacts.py")
artifacts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(artifacts)
spec = importlib.util.spec_from_file_location("bundle_check", ROOT / "ci/check_artifacts.py")
bundle_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bundle_check)


def fixture(root):
    work = root / "job"
    receipt = work / "cgrep/build/verify.abcdef"
    receipt.mkdir(parents=True)
    (work / "acceptance.log").write_text(f"Acceptance receipts: {receipt}\nacceptance: PASS\n")
    (work / "toolchain-selftest.log").write_text("selftest: PASS\n")
    for name in artifacts.RECEIPTS:
        (receipt / name).write_text('{"schema":"fixture"}\n')
    (work / ".runner").write_text("secret registration data")
    (work / "tea.yaml").write_text("secret authentication data")
    return work, receipt


class ArtifactTests(unittest.TestCase):
    def test_allowlist_survives_job_cleanup(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work, _ = fixture(root)
            output = root / "upload"
            metadata = artifacts.collect(work, output, 0, "a" * 40)
            self.assertTrue(metadata["complete_success"])
            self.assertEqual(set(p.name for p in output.iterdir()), set(artifacts.LOGS + artifacts.RECEIPTS + ["metadata.json"]))
            shutil.rmtree(work)
            self.assertEqual((output / "cgrep.json").read_text(), '{"schema":"fixture"}\n')
            self.assertNotIn("secret", "".join(p.read_text() for p in output.iterdir()))
            self.assertEqual(bundle_check.check(output), metadata)
            for entry in metadata["files"]:
                self.assertEqual(hashlib.sha256((output / entry["path"]).read_bytes()).hexdigest(), entry["sha256"])

    def test_failure_without_receipts_is_marked(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work = root / "job"
            work.mkdir()
            (work / "toolchain-selftest.log").write_text("seeded selftest failure\n")
            result = artifacts.collect(work, root / "upload", 7, "")
            self.assertEqual(result["acceptance_exit_code"], 7)
            self.assertFalse(result["complete_success"])
            with self.assertRaises(ValueError):
                artifacts.collect(work, root / "false-pass", 0, "")

    def test_receipt_escape_and_symlink_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work, receipt = fixture(root)
            (work / "acceptance.log").write_text(f"Acceptance receipts: {root}\n")
            with self.assertRaises(ValueError):
                artifacts.collect(work, root / "escape", 1, "")
            (work / "acceptance.log").write_text(f"Acceptance receipts: {receipt}\n")
            (receipt / "cli.json").unlink()
            (receipt / "cli.json").symlink_to(work / ".runner")
            with self.assertRaises(ValueError):
                artifacts.collect(work, root / "link", 0, "")

    def test_exit_trap_preserves_failure(self):
        text = (ROOT / "ci/verify.sh").read_text()
        trap = text[text.index("collect_receipts() {"):text.index("cp -a")]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work, _ = fixture(root)
            script = 'set -euo pipefail\nroot="$1"\nwork="$2"\n' + trap + '\nexit 7\n'
            sandbox = root / "script"
            (sandbox / "ci").mkdir(parents=True)
            shutil.copyfile(ROOT / "ci/collect_artifacts.py", sandbox / "ci/collect_artifacts.py")
            result = subprocess.run(["bash", "-c", script, "fixture", str(sandbox), str(work)], capture_output=True)
            self.assertEqual(result.returncode, 7)
            metadata = json.loads((sandbox / "ci-artifacts/metadata.json").read_text())
            self.assertEqual(metadata["acceptance_exit_code"], 7)

    def test_collection_failure_preserves_original_failure(self):
        text = (ROOT / "ci/verify.sh").read_text()
        trap = text[text.index("collect_receipts() {"):text.index("cp -a")]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work, receipt = fixture(root)
            (receipt / "cli.json").unlink()
            (receipt / "cli.json").symlink_to(work / ".runner")
            script = 'set -euo pipefail\nroot="$1"\nwork="$2"\n' + trap + '\nexit 7\n'
            sandbox = root / "script"
            (sandbox / "ci").mkdir(parents=True)
            shutil.copyfile(ROOT / "ci/collect_artifacts.py", sandbox / "ci/collect_artifacts.py")
            result = subprocess.run(["bash", "-c", script, "fixture", str(sandbox), str(work)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 7)
            self.assertIn("unsafe", result.stderr)
            self.assertFalse((sandbox / "ci-artifacts/metadata.json").exists())

    def test_collection_failure_cannot_pass(self):
        text = (ROOT / "ci/verify.sh").read_text()
        trap = text[text.index("collect_receipts() {"):text.index("cp -a")]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work, _ = fixture(root)
            (work / "toolchain-selftest.log").unlink()
            script = 'set -euo pipefail\nroot="$1"\nwork="$2"\n' + trap + '\nexit 0\n'
            sandbox = root / "script"
            (sandbox / "ci").mkdir(parents=True)
            shutil.copyfile(ROOT / "ci/collect_artifacts.py", sandbox / "ci/collect_artifacts.py")
            result = subprocess.run(["bash", "-c", script, "fixture", str(sandbox), str(work)], capture_output=True)
            self.assertNotEqual(result.returncode, 0)


class BundleTests(unittest.TestCase):
    def test_same_length_tamper_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work, _ = fixture(root)
            output = root / "bundle"
            artifacts.collect(work, output, 0, "a" * 40)
            path = output / "cgrep.json"
            path.write_bytes(path.read_bytes().replace(b"fixture", b"fixturE"))
            with self.assertRaisesRegex(ValueError, "hash mismatch"):
                bundle_check.check(output)

    def test_inventory_and_status_tampering_rejected(self):
        for defect in ["duplicate", "escape", "unlisted", "symlink", "missing", "status", "bool-status"]:
            with self.subTest(defect=defect), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                work, _ = fixture(root)
                output = root / "bundle"
                metadata = artifacts.collect(work, output, 0, "a" * 40)
                if defect == "duplicate":
                    metadata["files"] = [metadata["files"][0], metadata["files"][0]]
                elif defect == "escape":
                    metadata["files"][0]["path"] = "../.runner"
                elif defect == "unlisted":
                    (output / "tea.yaml").write_text("seeded secret")
                elif defect == "symlink":
                    (output / "cli.json").unlink()
                    (output / "cli.json").symlink_to(work / ".runner")
                elif defect == "missing":
                    (output / "cli.json").unlink()
                    metadata["files"] = [item for item in metadata["files"] if item["path"] != "cli.json"]
                elif defect == "status":
                    metadata["complete_success"] = False
                else:
                    metadata["acceptance_exit_code"] = True
                (output / "metadata.json").write_text(json.dumps(metadata))
                with self.assertRaises(ValueError):
                    bundle_check.check(output)

    def test_metadata_and_file_bounds(self):
        self.assertEqual(bundle_check.FILES, set(artifacts.LOGS + artifacts.RECEIPTS))
        self.assertEqual(bundle_check.MAX_FILE, artifacts.MAX_FILE)
        for name in ["metadata.json", "acceptance.log"]:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                work, _ = fixture(root)
                output = root / "bundle"
                artifacts.collect(work, output, 0, "a" * 40)
                with (output / name).open("r+b") as stream:
                    stream.truncate(bundle_check.MAX_FILE + 1)
                with self.assertRaisesRegex(ValueError, "oversized"):
                    bundle_check.check(output)

    def test_metadata_and_directory_symlinks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work, _ = fixture(root)
            output = root / "bundle"
            artifacts.collect(work, output, 0, "a" * 40)
            alias = root / "alias"
            alias.symlink_to(output, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, "regular directory"):
                bundle_check.check(alias)
            (output / "metadata.json").unlink()
            (output / "metadata.json").symlink_to(work / ".runner")
            with self.assertRaisesRegex(ValueError, "unsafe"):
                bundle_check.check(output)

    def test_partial_failure_is_valid_evidence_not_success(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work = root / "job"
            work.mkdir()
            (work / "acceptance.log").write_text("seeded failure\n")
            output = root / "bundle"
            artifacts.collect(work, output, 7, "")
            self.assertEqual(bundle_check.check(output)["acceptance_exit_code"], 7)
            result = subprocess.run(["python3", "-I", str(ROOT / "ci/check_artifacts.py"), str(output), "--receipts"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("recorded acceptance failed", result.stderr)

    def test_hashes_do_not_prove_acceptance_receipts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work, _ = fixture(root)
            output = root / "bundle"
            artifacts.collect(work, output, 0, "a" * 40)
            self.assertTrue(bundle_check.check(output)["complete_success"])
            result = subprocess.run(["python3", "-I", str(ROOT / "ci/check_artifacts.py"), str(output), "--receipts"], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("schema", result.stderr)

    def test_local_archive_retrieval_after_job_cleanup(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work, _ = fixture(root)
            output = root / "bundle"
            expected = artifacts.collect(work, output, 0, "a" * 40)
            archive = root / "artifact.zip"
            with zipfile.ZipFile(archive, "w") as stream:
                for path in output.iterdir():
                    stream.write(path, path.name)
            shutil.rmtree(work)
            shutil.rmtree(output)
            retrieved = root / "retrieved"
            with zipfile.ZipFile(archive) as stream:
                stream.extractall(retrieved)
            self.assertEqual(bundle_check.check(retrieved), expected)


if __name__ == "__main__":
    unittest.main()
