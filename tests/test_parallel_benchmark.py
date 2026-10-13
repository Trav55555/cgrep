"""The performance comparator permits file reordering, not wrong output."""
import importlib.util
from pathlib import Path
import subprocess
import unittest

spec = importlib.util.spec_from_file_location("parallel_benchmark", Path(__file__).with_name("benchmark_parallel.py"))
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


class OutputTests(unittest.TestCase):
    def setUp(self):
        self.expected = {"a": b"a:1:x\na:2:x\n", "b": b"b:1:x\n"}
        self.paths = ["a", "b"]

    def check(self, output, parallel=True, status=0, stderr=b""):
        result = subprocess.CompletedProcess([], status, output, stderr)
        benchmark.validate_output(result, self.expected, self.paths, parallel)

    def test_rg_file_reordering(self):
        self.check(self.expected["b"] + self.expected["a"])

    def test_cgrep_file_order(self):
        with self.assertRaises(RuntimeError):
            self.check(self.expected["b"] + self.expected["a"], parallel=False)

    def test_per_file_line_order(self):
        with self.assertRaises(RuntimeError):
            self.check(b"a:2:x\na:1:x\nb:1:x\n")

    def test_lost_line(self):
        with self.assertRaises(RuntimeError):
            self.check(b"a:1:x\nb:1:x\n")

    def test_duplicate_line(self):
        with self.assertRaises(RuntimeError):
            self.check(self.expected["a"] * 2 + self.expected["b"])

    def test_exit_status(self):
        with self.assertRaises(RuntimeError):
            self.check(self.expected["a"] + self.expected["b"], status=1)

    def test_reference(self):
        actual = benchmark.reference_output("a", b"no\nx\nx", b"x", True, True)
        self.assertEqual(actual, b"a:2:x\na:3:x\n")


    def test_reference_without_numbers(self):
        actual = benchmark.reference_output("a", b"no\nx\nx", b"x", True, False)
        self.assertEqual(actual, b"a:x\na:x\n")

    def test_cgrep_worker_sweep(self):
        variants = benchmark.commands("cgrep", "-needle", ["a", "b"], True, "/usr/bin/grep", cgrep_threads=True)
        for threads in [1, 2, 4, 8]:
            self.assertEqual(variants[f"cgrep-j{threads}"],
                             ["cgrep", "-n", f"-j{threads}", "--", "-needle", "a", "b"])
        self.assertNotIn("-j8", variants["gnu-grep"])
        self.assertNotIn("-j8", variants["cgrep"])

    def test_gnu_literal_and_number_flags(self):
        variants = benchmark.commands("cgrep", "-needle", ["a", "b"], True, "/usr/bin/grep")
        self.assertIn("-F", variants["gnu-grep"])
        self.assertIn("-n", variants["gnu-grep"])
        self.assertIn("--", variants["gnu-grep"])
        self.assertIn("-j8", variants["rg-j8"])


if __name__ == "__main__":
    unittest.main()
