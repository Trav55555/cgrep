"""Failure-sensitive output/reference and realistic-corpus checks."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("realistic", Path(__file__).with_name("benchmark_realistic.py"))
realistic = importlib.util.module_from_spec(spec)
spec.loader.exec_module(realistic)


class RealisticTests(unittest.TestCase):
    def test_lf_only_reference(self):
        self.assertEqual(realistic.reference_output("a", b"x\vx\rx\r\nx", b"x", True, True),
                         b"a:1:x\vx\rx\r\na:2:x\n")
        self.assertEqual(realistic.reference_output("a", b"", b"", False, False), b"")
        self.assertEqual(realistic.reference_output("a", b"\n", b"", False, False), b"\n")

    def test_unterminated_rg_output_rejected(self):
        result = subprocess.CompletedProcess([], 0, b"b:x\na:x", b"")
        with self.assertRaises(RuntimeError):
            realistic.validate_output(result, {"a": b"a:x\n", "b": b"b:x\n"}, ["a", "b"], True)

    def test_fixed_baseline_worker_counts(self):
        commands = realistic.commands("candidate", "x", ["a", "b"], True, "grep", "control", True)
        for jobs in [1, 2, 4, 8]:
            self.assertEqual(commands[f"baseline-j{jobs}"], ["control", "-n", f"-j{jobs}", "--", "x", "a", "b"])

    def test_shapes_and_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for kind in ["mixed", "uneven"]:
                rows, excluded = realistic.corpus(kind, 1, None)
                directory = root / kind
                directory.mkdir()
                names, expected, manifest, digest = realistic.prepare(directory, rows, "needle42", True)
                self.assertFalse(excluded)
                self.assertGreater(len(set(len(data) for _, data in rows)), 4)
                self.assertEqual(len(names), len(manifest))
                self.assertEqual(len(digest), 64)
                self.assertTrue(any(expected.values()))
                self.assertEqual(sum(item["bytes"] for item in manifest), sum((directory / n).stat().st_size for n in names))

    def test_cold_advice_is_targeted(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "copy").write_bytes(b"fixture")
            (root / "untouched").write_bytes(b"outside measured corpus")
            with patch.object(realistic.os, "posix_fadvise") as advice, patch.object(realistic.os, "fsync") as sync:
                realistic.advise(root, ["copy"])
                self.assertEqual(advice.call_count, 1)
                self.assertEqual(sync.call_count, 1)
                self.assertEqual(advice.call_args.args[1:], (0, 0, realistic.os.POSIX_FADV_DONTNEED))
            self.assertEqual((root / "untouched").read_bytes(), b"outside measured corpus")

    def test_wrong_same_length_bytes_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            fake = directory / "fake"
            fake.write_text("#!/bin/sh\nprintf 'b:y\\n'\n")
            fake.chmod(0o755)
            for sink in ["file", "pipe"]:
                with self.assertRaises(RuntimeError):
                    realistic.invocation([str(fake)], directory, {}, sink, {"b": b"b:x\n"}, ["b"], False)


if __name__ == "__main__":
    unittest.main()
