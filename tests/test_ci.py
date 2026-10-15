"""Check the narrow trusted-runner and non-weakened acceptance contract."""
import importlib.util
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fetch', ROOT / 'ci/fetch_dependencies.py')
fetch = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetch)


def workflow_contract(text):
    required = ["  push:\n    branches: [main]", "runs-on: cgrep-x86_64-3g",
                "github.event_name == 'push'", "github.repository == 'trav/cgrep'",
                "github.actor == 'trav'", "cancel-in-progress: false", "run: bash ci/verify.sh",
                "persist-credentials: false"]
    if any(token not in text for token in required):
        raise ValueError("trusted push/runner/acceptance condition missing")
    artifact_requirements = ["if: always()", "path: ci-artifacts/", "if-no-files-found: error", "retention-days: 30", "uses: actions/upload-artifact@a8a3f3ad30e3422c9c7b888a15615d19a852ae32"]
    if any(token not in text for token in artifact_requirements) or not re.search(r"uses: actions/upload-artifact@[0-9a-f]{40}\n", text):
        raise ValueError("missing mandatory pinned artifact retention")
    if "pull_request" in text or "deploy" in text or not re.search(r"uses: actions/checkout@[0-9a-f]{40}\n", text):
        raise ValueError("untrusted trigger, deployment, or unpinned action")


def script_contract(text):
    required = ["set -euo pipefail", "ulimit -c 0", "MemoryMax=3G", "MemorySwapMax=0", "flock 9",
                '"$toolchain/bin/dcheck-tools" check', 'make -j1 -C "$toolchain" selftest',
                'make -j1 -C "$work/cgrep" verify', 'python3 -I "$root/ci/fetch_dependencies.py"']
    if any(token not in text for token in required):
        raise ValueError("required pin/profile/limit/failure-sensitive gate missing")
    if "continue-on-error" in text or "|| true" in text:
        raise ValueError("CI cannot discard required failures")


class CITests(unittest.TestCase):
    def test_trusted_pinned_workflow(self):
        text = (ROOT / ".gitea/workflows/ci.yml").read_text()
        workflow_contract(text)
        for old, new in [("github.actor == 'trav'", "true"),
                         ("  push:", "  pull_request:"),
                         ("cgrep-x86_64-3g", "ubuntu-latest")]:
            with self.assertRaises(ValueError):
                workflow_contract(text.replace(old, new))

    def test_artifact_step_is_sensitive(self):
        text = (ROOT / ".gitea/workflows/ci.yml").read_text()
        for token in ["if: always()", "if-no-files-found: error", "path: ci-artifacts/", "a8a3f3ad30e3422c9c7b888a15615d19a852ae32"]:
            with self.assertRaises(ValueError):
                workflow_contract(text.replace(token, "seeded missing retention"))

    def test_required_checks_are_sensitive(self):
        text = (ROOT / "ci/verify.sh").read_text()
        script_contract(text)
        for old in ["MemorySwapMax=0", '"$toolchain/bin/dcheck-tools" check',
                    'make -j1 -C "$work/cgrep" verify', "set -euo pipefail"]:
            with self.assertRaises(ValueError):
                script_contract(text.replace(old, "seeded missing requirement"))

    def test_logged_pipeline_preserves_failure(self):
        script = (ROOT / "ci/verify.sh").read_text()
        preamble = next(line for line in script.splitlines() if line.startswith("set -"))
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / "failure.log"
            result = subprocess.run(["bash", "-c", preamble + '\n(printf "seeded failure\\n"; exit 7) | tee "$1"\nprintf "false PASS\\n"',
                                     "sensor", str(log)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 7)
            self.assertEqual(log.read_text(), "seeded failure\n")
            self.assertNotIn("false PASS", result.stdout)

    def test_exact_sibling_pins(self):
        config = json.loads((ROOT / "ci/dependencies.json").read_text())
        self.assertEqual(config["schema"], "cgrep.ci-dependencies/0")
        self.assertEqual(set(config), {"schema", "c-utils", "dependable-c-agent-toolchain"})
        for name in ["c-utils", "dependable-c-agent-toolchain"]:
            self.assertEqual(config[name]["url"], f"https://git.traversannan.com/trav/{name}.git")
            self.assertRegex(config[name]["revision"], r"^[0-9a-f]{40}$")
            self.assertRegex(config[name]["tree"], r"^[0-9a-f]{40}$")
        self.assertEqual(config, fetch.configuration(ROOT / "ci/dependencies.json"))

    def test_unsafe_archive_rejected_before_extraction(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for i, name in enumerate(["root/../../outside", "root/.git/config", "/absolute"]):
                archive = root / f"bad{i}.tar.gz"
                with tarfile.open(archive, "w:gz") as stream:
                    member = tarfile.TarInfo(name)
                    member.size = 1
                    stream.addfile(member, io.BytesIO(b"x"))
                target = root / f"target{i}"
                with self.assertRaises(ValueError):
                    fetch.extract(archive, target, "0" * 40)
                self.assertFalse(target.exists())

    def test_source_tree_tamper_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"
            source.mkdir()
            (source / "fixture").write_bytes(b"original")
            subprocess.run(["git", "init", "-q", str(source)], check=True)
            subprocess.run(["git", "-C", str(source), "add", "."], check=True)
            tree = subprocess.check_output(["git", "-C", str(source), "write-tree"], text=True).strip()
            for i, data in enumerate([b"original", b"tampered"]):
                archive = root / f"source{i}.tar.gz"
                with tarfile.open(archive, "w:gz") as stream:
                    member = tarfile.TarInfo("root/fixture")
                    member.mode = 0o644
                    member.size = len(data)
                    stream.addfile(member, io.BytesIO(data))
                if i == 0:
                    fetch.extract(archive, root / f"target{i}", tree)
                else:
                    with self.assertRaises(ValueError):
                        fetch.extract(archive, root / f"target{i}", tree)


if __name__ == "__main__":
    unittest.main()
