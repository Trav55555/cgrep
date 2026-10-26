"""Check the narrow trusted-runner and non-weakened acceptance contract."""
import hashlib
import importlib.util
import os
import shutil
import signal
import sys
import io
import json
from pathlib import Path
import re
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock

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
    nightly = ["  schedule:\n    - cron: '0 7 * * *'\n  workflow_dispatch:\n",
               "github.repository == 'trav/cgrep' && (github.event_name == 'schedule' ||\n"
               "      ((github.event_name == 'push' || github.event_name == 'workflow_dispatch') && github.actor == 'trav'))",
               "run: bash ci/verify.sh ${{ github.event_name == 'push' && 'gate' || 'deep' }}\n"]
    if any(token not in text for token in nightly):
        raise ValueError("nightly trigger, its trust condition, or the tier selection missing")
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
    acceptance = ('python3 -I "$work/cgrep/ci/reference.py" exec -- \\\n'
                  '    timeout --signal=TERM --kill-after=30s 30m make -j1 -C "$work/cgrep" verify \\\n'
                  '    CUTILS="$work/c-utils" DCHECK="$toolchain/bin/dcheck" VERIFY_TIER="$tier" \\\n'
                  '    2>&1 | tee "$work/acceptance.log"')
    if acceptance not in text:
        raise ValueError("checked reference must wrap the timed acceptance pipeline")
    if "continue-on-error" in text or "|| true" in text:
        raise ValueError("CI cannot discard required failures")


class CITests(unittest.TestCase):
    def test_trusted_pinned_workflow(self):
        text = (ROOT / ".gitea/workflows/ci.yml").read_text()
        workflow_contract(text)
        for old, new in [("github.actor == 'trav'", "true"),
                         ("  push:", "  pull_request:"),
                         ("cgrep-x86_64-3g", "ubuntu-latest"),
                         ("&& github.actor == 'trav'))", "))"),
                         ("github.event_name == 'workflow_dispatch') &&", "github.event_name == 'workflow_dispatch') ||"),
                         ("'gate' || 'deep'", "'deep' || 'gate'"),
                         ("'0 7 * * *'", "'0 * * * *'")]:
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

    def test_reference_selection_is_in_acceptance_pipeline(self):
        text = (ROOT / 'ci/verify.sh').read_text()
        prefix = 'python3 -I "$work/cgrep/ci/reference.py" exec -- \\\n    '
        for changed in [text.replace(prefix, ''),
                        prefix + 'true\n' + text.replace(prefix, ''),
                        text.replace('exec -- \\\n    timeout', 'exec -- true\n    timeout')]:
            with self.assertRaises(ValueError):
                script_contract(changed)

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

    def test_tier_is_required(self):
        for tier in [[], ["fast"], ["gate deep"]]:
            result = subprocess.run(["bash", str(ROOT / "ci/verify.sh"), *tier], capture_output=True, text=True)
            self.assertEqual(result.returncode, 2, tier)
            self.assertIn("usage: ci/verify.sh gate|deep", result.stderr)
        with tempfile.TemporaryDirectory() as temporary:
            env = dict(os.environ, CUTILS=temporary, BUILD=temporary, VERIFY_TIER="fast")
            result = subprocess.run(["bash", str(ROOT / "tests/verify.sh"), "--profiled"],
                                    capture_output=True, text=True, env=env)
            self.assertEqual(result.returncode, 2)
            self.assertIn("VERIFY_TIER must be gate or deep", result.stderr)

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


class ReferenceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.wrapper = self.root / 'reference.py'
        shutil.copyfile(ROOT / 'ci/reference.py', self.wrapper)
        self.manifest = self.root / 'reference.json'
        self.pin = json.loads((ROOT / 'ci/reference.json').read_text())
        self.source = self.root / 'source'
        self.marker = self.root / 'executed'
        self.source.write_text('#!/bin/sh\n'
                               f'echo executed >> "{self.marker}"\n'
                               'echo "fixture version"\n')
        self.source.chmod(0o700)
        self.pin['sha256'] = hashlib.sha256(self.source.read_bytes()).hexdigest()
        self.pin['version_first_line'] = 'fixture version'
        self.save_pin()
        self.cache = self.root / 'cache'
        self.target = self.cache / self.pin['sha256'] / 'bin/rg'

    def save_pin(self):
        self.manifest.write_text(json.dumps(self.pin))

    def run_cli(self, action, *args, env=None):
        return subprocess.run([sys.executable, '-I', str(self.wrapper), action,
                               '--cache-root', str(self.cache), *map(str, args)],
                              capture_output=True, text=True, env=env)

    def provision(self):
        return self.run_cli('provision', '--source', self.source)

    def test_production_pin(self):
        pin = json.loads((ROOT / 'ci/reference.json').read_text())
        self.assertEqual({k: v for k, v in pin.items() if k != 'provenance'}, {
            'schema': 'cgrep.ci-reference/0', 'platform': 'linux-x86_64', 'name': 'rg',
            'sha256': 'e62198eb19b136b88c330af83647b5a962cb99b6b1f066758568f12de1974849',
            'version_first_line': 'ripgrep 15.2.0 (rev e89fff89ac)'})
        self.assertTrue(pin['provenance'].strip())

    def test_strict_manifest(self):
        for key, value in [('extra', 'bad'), ('schema', 'bad'), ('platform', 'darwin'),
                           ('name', 'other'), ('sha256', 'A' * 64), ('provenance', '')]:
            original = dict(self.pin)
            self.pin[key] = value
            self.save_pin()
            self.assertNotEqual(self.provision().returncode, 0)
            self.pin = original
        self.assertFalse(self.marker.exists())

    def test_wrong_hash_never_executes(self):
        self.source.write_text(self.source.read_text() + '# changed\n')
        self.assertNotEqual(self.provision().returncode, 0)
        self.assertFalse(self.marker.exists())
        self.assertFalse(self.target.exists())

    def test_wrong_version_not_published(self):
        self.pin['version_first_line'] = 'wrong version'
        self.save_pin()
        self.assertNotEqual(self.provision().returncode, 0)
        self.assertTrue(self.marker.exists())
        self.assertFalse(self.target.exists())

    def test_copied_bytes_validated_not_source_path(self):
        self.source.write_text('#!/bin/sh\n'
                               f'test "$0" != "{self.source}" || exit 9\n'
                               'echo "fixture version"\n')
        self.pin['sha256'] = hashlib.sha256(self.source.read_bytes()).hexdigest()
        self.save_pin()
        self.target = self.cache / self.pin['sha256'] / 'bin/rg'
        self.assertEqual(self.provision().returncode, 0)
        self.assertEqual(self.target.read_bytes(), self.source.read_bytes())
        self.source.unlink()
        self.assertEqual(self.run_cli('exec', '--', 'rg', '--version').returncode, 0)

    def test_corrupted_copy_rejected_before_execution(self):
        spec = importlib.util.spec_from_file_location('reference', self.wrapper)
        reference = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(reference)
        target = reference.target_path(self.cache, self.pin, create=True)

        def corrupt_copy(source, output):
            output.write(source.read() + b'# corruption during copy\n')

        with mock.patch.object(reference.shutil, 'copyfileobj', corrupt_copy):
            with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
                reference.provision(self.source, target, self.pin)
        self.assertFalse(self.marker.exists())
        self.assertFalse(target.exists())
        self.assertEqual(list(target.parent.iterdir()), [])

    def test_repeat_and_corruption(self):
        self.assertEqual(self.provision().returncode, 0)
        inode = self.target.stat().st_ino
        self.assertEqual(self.provision().returncode, 0)
        self.assertEqual(self.target.stat().st_ino, inode)
        self.marker.unlink()
        self.target.write_text('corrupt')
        self.assertNotEqual(self.run_cli('exec', '--', 'true').returncode, 0)
        self.assertNotEqual(self.provision().returncode, 0)
        self.assertEqual(self.target.read_text(), 'corrupt')
        self.assertFalse(self.marker.exists())

    def test_missing_cache_no_fallback_or_install(self):
        self.assertNotEqual(self.run_cli('exec', '--', 'true').returncode, 0)
        self.assertFalse(self.cache.exists())

    def test_selection_exit_and_signal(self):
        self.assertEqual(self.provision().returncode, 0)
        ambient = self.root / 'ambient'
        ambient.mkdir()
        (ambient / 'rg').write_text('#!/bin/sh\necho ambient\nexit 9\n')
        (ambient / 'rg').chmod(0o700)
        env = dict(os.environ, PATH=str(ambient) + ':' + os.environ['PATH'])
        selected = self.run_cli('exec', '--', 'rg', '--version', env=env)
        self.assertEqual(selected.returncode, 0)
        self.assertEqual(selected.stdout.strip(), 'fixture version')
        self.assertIn(self.pin['sha256'], selected.stderr)
        self.assertEqual(self.run_cli('exec', '--', '/bin/sh', '-c', 'exit 7').returncode, 7)
        self.assertEqual(self.run_cli('exec', '--', '/bin/sh', '-c', 'kill -TERM $$').returncode,
                         -signal.SIGTERM)
        self.assertEqual(self.run_cli('exec', '--', 'nonexistent-cgrep-command').returncode, 127)
        self.assertNotEqual(self.run_cli('exec', 'true').returncode, 0)

    def test_symlink_target_rejected(self):
        self.assertEqual(self.provision().returncode, 0)
        self.target.unlink()
        self.target.symlink_to(self.source)
        self.marker.unlink()
        self.assertNotEqual(self.provision().returncode, 0)
        self.assertNotEqual(self.run_cli('exec', '--', 'true').returncode, 0)
        self.assertFalse(self.marker.exists())


if __name__ == "__main__":
    unittest.main()
