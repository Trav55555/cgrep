#!/usr/bin/env python3
"""Check mutation selectors without invoking the real mutation runner."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("script", type=Path)
parser.add_argument("--root", type=Path, required=True)
parser.add_argument("--record", type=Path)
parser.add_argument("--baseline", type=Path)
args = parser.parse_args()
root = args.root.resolve()
script = args.script.resolve()
absolute = root / "src/cgrep.c"
assert absolute.is_file()
cases = [("unset", None), ("empty", ""), ("cgrep", "src/cgrep.c"),
         ("ignore", "src/ignore.inc"), ("absolute", str(absolute)),
         ("traversal", "../cgrep/src/cgrep.c"), ("unknown", "src/unknown.c")]
valid = {}
failures = []
with tempfile.TemporaryDirectory(prefix="cgrep-selector-") as tmp:
    tmp = Path(tmp)
    hook = tmp / "hook.py"
    hook.write_text("import json, os, sys\nfrom pathlib import Path\n"
                    "Path(os.environ['ARGV_LOG']).write_text(json.dumps(sys.argv[1:]))\n")
    for jobs, timeout in [("2", "30"), ("3", "47")]:
        for name, selector in cases:
            log = tmp / "argv.json"
            log.unlink(missing_ok=True)
            env = dict(os.environ, MUTANTS=str(hook), ARGV_LOG=str(log), CUTILS=str(tmp))
            for key in ("MUTATION_FILE", "JOBS", "MUTATION_TIMEOUT", "BASH_ENV", "ENV"):
                env.pop(key, None)
            if jobs != "2":
                env.update(JOBS=jobs, MUTATION_TIMEOUT=timeout)
            if selector is not None:
                env["MUTATION_FILE"] = selector
            result = subprocess.run(["bash", str(script)], cwd=root, env=env,
                                    capture_output=True, text=True, timeout=10)
            argv = json.loads(log.read_text()) if log.exists() else None
            key = f"{name}/{jobs}/{timeout}"
            ok = result.returncode == 2 and argv is None
            if name in {"unset", "empty", "cgrep", "ignore"}:
                files = ["src/cgrep.c", "src/ignore.inc"] if name in {"unset", "empty"} else [selector]
                prefix = ["--root", "."] + [part for f in files for part in ("--file", f)]
                prefix += ["--jobs", jobs, "--timeout", timeout, "--build"]
                ok = (result.returncode == 0 and argv is not None
                      and argv[:len(prefix)] == prefix and len(argv) == len(prefix) + 3
                      and argv[-2] == "--cmd")
                valid[key] = argv
            print(f"{'PASS' if ok else 'FAIL'} {key} status={result.returncode} hook={argv is not None}")
            if result.stderr:
                print(result.stderr.rstrip())
            if not ok:
                failures.append(key)
if args.record:
    args.record.write_text(json.dumps(valid, indent=2) + "\n")
if args.baseline:
    assert valid == json.loads(args.baseline.read_text()), "valid argv changed"
    print("PASS exact valid argv parity (including build and command gates)")
print(f"{len(cases) * 2 - len(failures)} passed; {len(failures)} failed")
raise SystemExit(bool(failures))
