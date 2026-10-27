"""ci/findings.py: the nightly verdict, the findings report and the issue
calls, with Gitea replaced by a recorder; and its wiring in ci/verify.sh."""
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("findings", ROOT / "ci/findings.py")
findings = importlib.util.module_from_spec(spec)
spec.loader.exec_module(findings)
spec = importlib.util.spec_from_file_location("check_receipt", ROOT / "tests/check_receipt.py")
check_receipt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check_receipt)


def result(status="pass", checks=()):
    failed = [c["name"] for c in checks if c["status"] == "fail"]
    return {"schema": "dcheck.result/0", "tier": "deep", "status": status, "failed": failed,
            "skipped": [c["name"] for c in checks if c["status"] == "skip"], "checks": list(checks)}


FUZZ = {"name": "fuzz", "status": "pass"}
SEMANTICS_FAIL = {"name": "semantics", "status": "fail", "detail": "32 files",
                  "log": ".dcheck/logs/semantics.log",
                  "diagnostics": [{"text": "DCHECK: semantics/sem_x.c failed under cerberus"},
                                  {"text": "src/a.c:7:3: error[rg-unchecked]: x", "file": "src/a.c",
                                   "line": 7, "column": 3, "severity": "error", "message": "x",
                                   "rule": "rg-unchecked"}]}


class Recorder(findings.Gitea):
    """Answers the issue calls from a list of open issues and records them."""

    def __init__(self, open_issues=(), fail=False):
        super().__init__("login", "owner/repo")
        self.open_issues, self.fail, self.calls = list(open_issues), fail, []

    def call(self, method, path, body=None, expect="id", applied=None):
        if self.fail:
            raise RuntimeError("%s %s failed" % (method, path))
        self.calls.append((method, path.split("?")[0], body))
        if method == "GET":
            return self.open_issues
        return {"id": 1, "number": 9}


class JudgeTests(unittest.TestCase):
    def test_verdict(self):
        self.assertIs(findings.judge(None, [])[0], False)
        self.assertEqual(findings.judge(result("pass", [FUZZ]), ["fuzz"]), (True, []))
        # A required check that was skipped, or absent, fails the run.
        self.assertEqual(findings.judge(result("pass", [{"name": "fuzz", "status": "skip"}]), ["fuzz"]),
                         (False, ["required check fuzz did not pass"]))
        self.assertEqual(findings.judge(result("pass", []), ["fuzz"]),
                         (False, ["required check fuzz did not pass"]))
        self.assertEqual(findings.judge(result("fail", [FUZZ, SEMANTICS_FAIL]), ["fuzz"]),
                         (False, ["dcheck deep: semantics"]))

    def test_acceptance_failure_after_a_passing_dcheck(self):
        # Differential tests run after dcheck; their failure is a finding too.
        self.assertEqual(findings.judge(result("pass", [FUZZ]), ["fuzz"], 1),
                         (False, ["acceptance exited 1"]))
        passed, reasons = findings.judge(None, [], 124)
        self.assertFalse(passed)
        self.assertEqual(reasons[0], "acceptance exited 124")

    def test_required_checks_match_the_receipt_policy(self):
        text = (ROOT / "ci/verify.sh").read_text()
        required = re.findall(r"--require (\S+)", text)
        self.assertEqual(len(required), len(set(required)))
        self.assertEqual(set(required), check_receipt.EXPECTED["deep"] - check_receipt.OPTIONAL)


class ReportTests(unittest.TestCase):
    def test_failed_check(self):
        with tempfile.TemporaryDirectory() as d:
            log = Path(d) / ".dcheck/logs/semantics.log"
            log.parent.mkdir(parents=True)
            log.write_text("".join("line %d\n" % i for i in range(100)) + "```end\n")
            body = findings.report(result("fail", [SEMANTICS_FAIL]), ["dcheck deep: semantics"], d,
                                   "abc", "https://h/r/1", None)
        self.assertIn("## semantics", body)
        self.assertIn("32 files", body)
        self.assertIn("- `src/a.c:7` [rg-unchecked] x", body)
        self.assertIn("- DCHECK: semantics/sem_x.c failed under cerberus", body)
        # The tail is the last 60 lines.
        self.assertIn("line 99", body)
        self.assertNotIn("line 39\n", body)
        # The log's own ``` cannot close the block it is quoted in.
        self.assertIn("````\n", body)

    def test_no_result_quotes_the_run_log(self):
        with tempfile.NamedTemporaryFile("w", suffix=".log") as run_log:
            run_log.write("make: *** [verify] Error 1\n")
            run_log.flush()
            body = findings.report(None, ["no dcheck result"], "/", "abc", "u", run_log.name)
        self.assertIn("make: *** [verify] Error 1", body)

    def test_truncated(self):
        huge = dict(SEMANTICS_FAIL, log=None, diagnostics=[{"text": "x" * 2000}] * 40)
        body = findings.report(result("fail", [huge]), [], "/", "abc", "u", None)
        self.assertLess(len(body), findings.MAX_BODY + 1)
        self.assertTrue(body.endswith("full logs)\n"))


class PublishTests(unittest.TestCase):
    issue = {"number": 4, "title": findings.TITLE}
    other = {"number": 5, "title": "Something else"}

    def test_pass_without_open_issue(self):
        g = Recorder([self.other])
        self.assertEqual(findings.publish(g, True, "b", "abc", "u"), "no open findings")
        self.assertEqual([c[0] for c in g.calls], ["GET"])

    def test_pass_closes(self):
        g = Recorder([self.other, self.issue])
        self.assertEqual(findings.publish(g, True, "b", "abc", "u"), "closed #4")
        self.assertEqual(g.calls[1][:2], ("POST", "/issues/4/comments"))
        self.assertEqual(g.calls[2], ("PATCH", "/issues/4", {"state": "closed"}))

    def test_failure_opens(self):
        g = Recorder([self.other])
        self.assertEqual(findings.publish(g, False, "body", "abc", "u"), "opened #9")
        self.assertEqual(g.calls[1], ("POST", "/issues", {"title": findings.TITLE, "body": "body"}))

    def test_failure_updates(self):
        g = Recorder([self.issue])
        self.assertEqual(findings.publish(g, False, "body", "abc", "u"), "updated #4")
        self.assertEqual(g.calls[1], ("PATCH", "/issues/4", {"body": "body"}))
        self.assertEqual(g.calls[2][:2], ("POST", "/issues/4/comments"))

    def test_tea_error_body_is_retried_then_raised(self):
        # tea exits 0 on an HTTP error, so the response's shape is the check.
        g = findings.Gitea("trav", "trav/cgrep", attempts=2, pause=0)
        error = subprocess.CompletedProcess([], 0, stdout='<html>bad gateway</html>', stderr="")
        with mock.patch.object(findings.subprocess, "run", return_value=error) as run:
            with self.assertRaises(RuntimeError):
                g.call("POST", "/issues", {"title": "t"}, expect="number")
        self.assertEqual(run.call_count, 2)
        self.assertEqual(run.call_args[0][0][:7],
                         ["tea", "api", "--login", "trav", "-X", "POST", "/repos/trav/cgrep/issues"])
        ok = subprocess.CompletedProcess([], 0, stdout='{"id":3,"number":2}', stderr="")
        with mock.patch.object(findings.subprocess, "run", return_value=ok):
            self.assertEqual(g.call("POST", "/issues", {"title": "t"}, expect="number")["number"], 2)


class Tea:
    """Stands in for subprocess.run: answers tea calls from a script of
    (method, path prefix) -> stdout or exception, and records them."""

    def __init__(self, script):
        self.script, self.calls = script, []

    def __call__(self, command, **kwargs):
        method, path = command[5], command[6]
        self.calls.append((method, path))
        for (m, prefix), answers in self.script.items():
            if m == method and path.startswith(prefix):
                answer = answers.pop(0) if len(answers) > 1 else answers[0]
                if isinstance(answer, Exception):
                    raise answer
                return subprocess.CompletedProcess(command, 0, stdout=answer, stderr="")
        raise AssertionError("unexpected call %s %s" % (method, path))


class RetryTests(unittest.TestCase):
    """A retry never repeats a create Gitea may already have applied; Gitea's
    own error is final; the retries stop at the deadline; every page of open
    issues is read."""
    ISSUE = json.dumps([{"number": 4, "title": findings.TITLE}])
    TIMEOUT = subprocess.TimeoutExpired(["tea"], 60)

    def test_lost_create_is_looked_up_not_repeated(self):
        tea = Tea({("GET", "/repos/o/r/issues"): ["[]", self.ISSUE], ("POST", "/repos/o/r/issues"): [self.TIMEOUT]})
        with mock.patch.object(findings.subprocess, "run", tea), mock.patch.object(findings.time, "sleep"):
            self.assertEqual(findings.publish(findings.Gitea("l", "o/r", attempts=5), False, "b", "abc", "u"), "opened #4")
        self.assertEqual([c[0] for c in tea.calls], ["GET", "POST", "GET"])

    def test_lost_comment_is_looked_up_not_repeated(self):
        posted = json.dumps([{"id": 7, "body": "Clean on `abc` ([run](u)); closing."}])
        tea = Tea({("GET", "/repos/o/r/issues/4/comments"): [posted], ("GET", "/repos/o/r/issues"): [self.ISSUE],
                   ("POST", "/repos/o/r/issues/4/comments"): [self.TIMEOUT],
                   ("PATCH", "/repos/o/r/issues/4"): ['{"number": 4}']})
        with mock.patch.object(findings.subprocess, "run", tea), mock.patch.object(findings.time, "sleep"):
            self.assertEqual(findings.publish(findings.Gitea("l", "o/r", attempts=5), True, "b", "abc", "u"), "closed #4")
        self.assertEqual([c[0] for c in tea.calls].count("POST"), 1)

    def test_gitea_error_is_not_retried(self):
        tea = Tea({("POST", "/repos/o/r/issues"): ['{"message": "body too large", "url": "x"}']})
        with mock.patch.object(findings.subprocess, "run", tea), mock.patch.object(findings.time, "sleep"):
            with self.assertRaisesRegex(RuntimeError, "body too large"):
                findings.Gitea("l", "o/r", attempts=5).call("POST", "/issues", {}, expect="number")
        self.assertEqual(len(tea.calls), 1)

    def test_no_answer_is_retried_until_the_deadline(self):
        tea = Tea({("GET", "/repos/o/r/issues"): ["<html>502</html>"]})
        clock = iter(range(0, 10000, 100))
        with mock.patch.object(findings.subprocess, "run", tea), mock.patch.object(findings.time, "sleep"), \
                mock.patch.object(findings.time, "monotonic", lambda: next(clock)):
            gitea = findings.Gitea("l", "o/r", attempts=50, pause=120, within=1000, timeout=60)
            with self.assertRaises(RuntimeError):
                gitea.open_issue()
        self.assertTrue(1 < len(tea.calls) < 50, len(tea.calls))

    def test_every_page_of_open_issues_is_read(self):
        full = json.dumps([{"number": n, "title": "other"} for n in range(100, 100 + findings.PAGE)])
        tea = Tea({("GET", "/repos/o/r/issues"): [full, self.ISSUE]})
        with mock.patch.object(findings.subprocess, "run", tea):
            self.assertEqual(findings.Gitea("l", "o/r").open_issue()["number"], 4)
        self.assertTrue(tea.calls[1][1].endswith("page=2"))

    def test_cut_never_leaves_a_quoted_log_open(self):
        with tempfile.TemporaryDirectory() as directory:
            checks = []
            for i in range(12):
                log = Path(directory) / ("%d.log" % i)
                log.write_text("".join("%d %s\n" % (n, "y" * 500) for n in range(60)))
                checks.append({"name": "c%d" % i, "status": "fail", "log": log.name})
            body = findings.report(result("fail", checks), [], directory, "abc", "u", None)
        self.assertLessEqual(len(body), findings.MAX_BODY)
        self.assertTrue(body.endswith("full logs)\n"))
        self.assertEqual(body.count("```\n") % 2, 0)


class MainTests(unittest.TestCase):
    def test_exit_status_is_the_verdict(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "cgrep.json"
            path.write_text(json.dumps(result("pass", [FUZZ])))
            base = ["--result", str(path), "--root", d, "--require", "fuzz", "--rev", "abc", "--run-url", "u"]
            with mock.patch("sys.stdout"):
                self.assertEqual(findings.main(base), 0)
                # dcheck-tools exec's profile line may precede the result on stdout.
                path.write_text("Verified checker profile: /p\n" + json.dumps(result("pass", [FUZZ])))
                self.assertEqual(findings.main(base), 0)
                self.assertEqual(findings.main(base + ["--require", "cbmc"]), 1)
                self.assertEqual(findings.main(base + ["--exit-status", "1"]), 1)
                # A Gitea failure does not change the verdict.
                with mock.patch.object(findings, "Gitea", lambda *a, **k: Recorder(fail=True)), mock.patch("sys.stderr"):
                    self.assertEqual(findings.main(base + ["--login", "l", "--repo", "o/r"]), 0)
                path.write_text("not json")
                self.assertEqual(findings.main(base), 1)


class WiringTests(unittest.TestCase):
    """ci/verify.sh's EXIT handler, run on a seeded early failure."""

    def run_trap(self, tier, status, cgrep=None):
        text = (ROOT / "ci/verify.sh").read_text()
        trap = text[text.index("collect_receipts() {"):text.index("cp -a")]
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        work = root / "job"
        work.mkdir()
        log = "".join("step %d\n" % i for i in range(80)) + "seeded failure\n"
        if cgrep is not None:
            # A complete run's evidence, so that collection succeeds.
            receipt = work / "cgrep/build/verify.abcdef"
            receipt.mkdir(parents=True)
            log = "Acceptance receipts: %s\n" % receipt + log
            (work / "toolchain-selftest.log").write_text("selftest: PASS\n")
            for name in ["c-utils.json", "cli.json"]:
                (receipt / name).write_text("{}\n")
            (receipt / "cgrep.json").write_text(json.dumps(cgrep))
        (work / "acceptance.log").write_text(log)
        sandbox = root / "script"
        (sandbox / "ci").mkdir(parents=True)
        for name in ["collect_artifacts.py", "findings.py"]:
            shutil.copyfile(ROOT / "ci" / name, sandbox / "ci" / name)
        script = 'set -euo pipefail\nroot="$1"\nwork="$2"\ntier="$3"\n' + trap + '\nexit %d\n' % status
        env = {"PATH": "/usr/bin:/bin"}
        return subprocess.run(["bash", "-c", script, "fixture", str(sandbox), str(work), tier],
                              capture_output=True, text=True, env=env), sandbox

    def test_deep_failure_before_dcheck_is_a_finding(self):
        run, sandbox = self.run_trap("deep", 7)
        self.assertEqual(run.returncode, 7)
        self.assertIn("acceptance exited 7", run.stdout)
        self.assertIn("no dcheck result", run.stdout)
        self.assertIn("seeded failure", run.stdout)
        self.assertNotIn("step 19\n", run.stdout)
        self.assertEqual(json.loads((sandbox / "ci-artifacts/metadata.json").read_text())["tier"], "deep")

    def test_deep_findings_fail_a_passing_acceptance(self):
        run, _ = self.run_trap("deep", 0, result("fail", [SEMANTICS_FAIL]))
        self.assertEqual(run.returncode, 1, run.stdout + run.stderr)
        self.assertIn("## semantics", run.stdout)
        passing = result("pass", [{"name": name, "status": "pass"}
                                  for name in check_receipt.EXPECTED["deep"]])
        run, _ = self.run_trap("deep", 0, passing)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        self.assertIn("findings: none", run.stdout)

    def test_gate_leaves_findings_alone(self):
        run, sandbox = self.run_trap("gate", 7)
        self.assertEqual(run.returncode, 7)
        self.assertNotIn("findings", run.stdout)
        self.assertEqual(json.loads((sandbox / "ci-artifacts/metadata.json").read_text())["tier"], "gate")


if __name__ == "__main__":
    unittest.main()
