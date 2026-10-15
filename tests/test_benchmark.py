"""Exercise the literal benchmark and its pre-timing correctness check."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class BenchmarkTests(unittest.TestCase):
    def run_benchmark(self, directory, bad_output=False):
        grep = shutil.which("grep")
        self.assertIsNotNone(grep)
        wrapper = Path(directory) / "grep"
        wrapper.write_text(
            f"#!{sys.executable}\n"
            "import os, sys\n"
            "assert os.environ['LC_ALL'] == 'C'\n"
            "if '--version' not in sys.argv:\n"
            "    assert sys.argv[1:3] == ['-F', '--']\n"
            + ("    print('incorrect benchmark output')\n    sys.exit(0)\n" if bad_output else "")
            + f"os.execv({grep!r}, [{grep!r}, *sys.argv[1:]])\n"
        )
        wrapper.chmod(0o755)
        env = dict(os.environ, LC_ALL="C.UTF-8",
                   PATH=directory + os.pathsep + os.environ["PATH"])
        return subprocess.run(
            [sys.executable, "-I", str(ROOT / "tests/benchmark.py"),
             str(ROOT / "build/cgrep"), "--mib", "1", "--repeats", "1"],
            capture_output=True, text=True, env=env, timeout=30,
        )

    def test_grep_measurements_and_c_locale(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_benchmark(directory)
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertTrue(report["grep"])
        self.assertEqual(report["locale"], "C")
        self.assertEqual(len(report["cases"]), 7)
        for case in report["cases"]:
            self.assertEqual(set(case["seconds"]), {"cgrep", "rg", "grep"})
            self.assertEqual(len(case["samples"]["grep"]), 1)
            self.assertGreater(case["seconds"]["grep"], 0)
            self.assertAlmostEqual(case["ratio_cgrep_to_grep"],
                                   case["seconds"]["cgrep"] / case["seconds"]["grep"])

    def test_incorrect_grep_output_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            result = self.run_benchmark(directory, bad_output=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("absent: grep produced an incorrect result", result.stderr)
        self.assertIn("incorrect benchmark output", result.stderr)
        self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
