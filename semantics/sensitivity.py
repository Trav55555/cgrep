"""Confirm bounded harnesses reject compiled finder and read-error defects.
Requires an explicit Cerberus executable/launcher; never edits the checkout.
"""
import argparse
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import tempfile

if not __debug__:
    raise SystemExit("Verification requires Python assertions; run without -O")

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--cerberus", required=True)
parser.add_argument("--cutils", type=Path, default=Path(__file__).resolve().parents[2] / "c-utils")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
cutils = args.cutils.resolve()
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
rejection = re.compile(r"undefined behaviour|assert\(\) failure", re.I)
errors = re.compile(r"undefined behaviour|STATIC ERROR|OTHER ERROR|^ERROR:|internal error", re.I | re.M)


def exercise(project, name, negative):
    source = project / "semantics" / (name + ".c")
    binary = project / name
    includes = ["-I" + str(project / "include"), "-I" + str(cutils / "include")]
    sources = list((project / "src").glob("*.c")) + list((cutils / "src").glob("*.c"))
    subprocess.run([os.environ.get("CC", "cc"), "-std=c89", "-pedantic-errors", "-Wall",
                    "-Wextra", "-Werror", *includes, "-o", str(binary), str(source),
                    *map(str, sources)], check=True, timeout=60, capture_output=True)
    native_env = dict(os.environ, TMPDIR=str(project))
    native = subprocess.run([str(binary)], env=native_env, capture_output=True, timeout=30)
    if negative:
        assert native.returncode == -6, (name, native.returncode, native.stderr)
    else:
        assert native.returncode == 0 and (name + ": PASS").encode() in native.stdout
    model_env = dict(os.environ)
    model_env.pop("TMPDIR", None)
    model = subprocess.run([args.cerberus, "--exec", *includes, str(source),
                            *map(str, sources)], env=model_env, capture_output=True, timeout=180)
    output = (model.stdout + model.stderr).decode(errors="replace")
    if negative:
        assert rejection.search(output), (name, model.returncode, output)
    else:
        assert model.returncode == 0 and not errors.search(output) and name + ": PASS" in output
    print(name, "compiled mutant rejected natively and by Cerberus" if negative else "baseline PASS",
          flush=True)


with tempfile.TemporaryDirectory(prefix="cgrep-semantics-sensitivity-") as work:
    base = Path(work)
    for name, old, new in [
        ("sem_find", "if (start > hay.len || needle.len > hay.len - start)",
         "if (start >= hay.len || needle.len > hay.len - start)"),
        ("sem_stream", "result.error = SCAN_IO;\n            result.system_error = cu_input_errno(in);",
         "result.error = SCAN_OK;\n            result.system_error = cu_input_errno(in);")
    ]:
        project = base / name
        project.mkdir()
        for directory in ["src", "include", "tests", "semantics"]:
            shutil.copytree(root / directory, project / directory)
        exercise(project, name, False)
        path = project / "src" / "cgrep.c"
        text = path.read_text()
        assert text.count(old) == 1
        path.write_text(text.replace(old, new))
        exercise(project, name, True)
print("bounded harness sensitivity: PASS", flush=True)
