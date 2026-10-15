"""CPU accounting and quietness rejection must distinguish background load."""
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch
import json
import tempfile

spec = importlib.util.spec_from_file_location("quiet", Path(__file__).with_name("benchmark_quiet.py"))
quiet = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quiet)


class QuietTests(unittest.TestCase):
    def test_guest_ticks_not_double_counted(self):
        self.assertEqual(quiet.cpu_ticks("cpu 100 20 30 400 10 5 15 0 60 5\n"),
                         {"total": 580, "busy": 170, "iowait": 10})

    def test_benchmark_scope_is_subtracted(self):
        before = {"cpu": {"total": 0, "busy": 0, "iowait": 0}, "usage_usec": 0}
        after = {"cpu": {"total": 1200, "busy": 800, "iowait": 0}, "usage_usec": 8000000,
                 "load": (5, 0, 0), "cpus": 12}
        sample = quiet.interference(before, after, 100)
        self.assertEqual(sample["background_percent"], 0)
        self.assertTrue(quiet.quiet(sample, 10, 0.5))
        after["usage_usec"] = 6000000
        sample = quiet.interference(before, after, 100)
        self.assertAlmostEqual(sample["background_percent"], 100 / 6)
        self.assertFalse(quiet.quiet(sample, 10, 0.5))

    def test_worker_load_does_not_reject_its_own_run(self):
        sample = {"background_percent": 0, "iowait_percent": 0, "load_per_cpu": 2}
        self.assertFalse(quiet.quiet(sample, 10, 0.5))
        self.assertTrue(quiet.quiet(sample, 10))
        sample["background_percent"] = 11
        self.assertFalse(quiet.quiet(sample, 10))

    def test_monitor_during_run_ignores_own_load(self):
        class Process:
            def __init__(self):
                self.started = False
            def poll(self):
                if self.started:
                    return 0
                self.started = True
                return None
            def wait(self):
                return 0

        def frame(total, busy, own, load):
            return {"cpu": {"total": total, "busy": busy, "iowait": 0},
                    "usage_usec": own, "load": (load, 0, 0), "cpus": 12}

        with tempfile.TemporaryDirectory() as temporary:
            report = Path(temporary) / "report.json"
            frames = [frame(0, 0, 0, 0), frame(1200, 0, 0, 0), frame(2400, 800, 8000000, 24)]
            argv = ["quiet", "--report", str(report), "--quiet-seconds", "1", "--wait-seconds", "2", "--", "fixture"]
            with patch.object(quiet.os, "sysconf", return_value=100), patch.object(quiet, "cgroup", return_value=Path("fixture")), patch.object(quiet, "snapshot", side_effect=frames), patch.object(quiet.time, "sleep"), patch.object(quiet.subprocess, "Popen", return_value=Process()), patch("sys.argv", argv):
                with self.assertRaises(SystemExit) as result:
                    quiet.main()
                self.assertEqual(result.exception.code, 0)
            self.assertTrue(json.loads(report.read_text())["quiet_eligible"])

    def test_invalid_counters_fail(self):
        before = {"cpu": {"total": 0, "busy": 0, "iowait": 0}, "usage_usec": 0}
        after = {"cpu": {"total": 0, "busy": 0, "iowait": 0}, "usage_usec": 0,
                 "load": (0, 0, 0), "cpus": 12}
        with self.assertRaises(ValueError):
            quiet.interference(before, after, 100)

    def test_boundary_and_pressure(self):
        sample = {"background_percent": 10, "iowait_percent": 2, "load_per_cpu": 0.5}
        self.assertTrue(quiet.quiet(sample, 10, 0.5))
        for key in sample:
            bad = dict(sample)
            bad[key] += 0.001
            self.assertFalse(quiet.quiet(bad, 10, 0.5))


if __name__ == "__main__":
    unittest.main()
