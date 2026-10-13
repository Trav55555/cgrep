"""Prove new uniform/counting regressions reject compiled defects.
Uses isolated copies and an explicit validated Cerberus launcher.
"""
import argparse
import os
from pathlib import Path
import resource
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def exercise(project, test, cutils, model, negative):
    binary = project / "sensor"
    sources = [*map(str, (project / "src").glob("*.c")), *map(str, (cutils / "src").glob("*.c"))]
    includes = ["-I" + str(project / "include"), "-I" + str(cutils / "include")]
    subprocess.run(["clang", "-std=c89", "-pedantic-errors", "-Wall", "-Wextra", *([] if negative else ["-Werror"]),
                    "-g", "-O1", "-UNDEBUG", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                    *includes, str(project / test), *sources, "-o", str(binary)], check=True, timeout=90)
    native = subprocess.run([str(binary)], capture_output=True, timeout=60)
    if negative:
        if native.returncode == 0 or b"Assertion" not in native.stderr:
            raise RuntimeError("compiled mutant did not fail its behavioral sensor")
    elif native.returncode != 0:
        raise RuntimeError(native.stderr.decode(errors="replace"))
    env = dict(os.environ)
    env.pop("TMPDIR", None)
    result = subprocess.run([model, "--exec", *includes, str(project / "semantics/sem_find.c"), *sources],
                            capture_output=True, env=env, timeout=180)
    output = (result.stdout + result.stderr).decode(errors="replace")
    if negative:
        if "assert() failure" not in output:
            raise RuntimeError("model did not reject the seeded contract defect: " + output)
    elif result.returncode != 0 or "sem_find: PASS" not in output or "ERROR" in output:
        raise RuntimeError("model baseline failed: " + output)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cerberus", required=True)
    parser.add_argument("--cutils", type=Path, default=ROOT.parent / "c-utils")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    mutations = [
        ("uniform-overstep", "tests/test_find.c", "at += needle.len;", "at += needle.len + 1;"),
        ("borrow-mask", "tests/test_count.c", "~(((word & low) + low) | word | low) & high", "(word - ones) & ~word & high"),
        ("lane-sum", "tests/test_count.c", "((sizeof word - 1) * CHAR_BIT)", "((sizeof word - 1) * CHAR_BIT - 1)"),
    ]
    with tempfile.TemporaryDirectory(prefix="cgrep-serial-sensitivity-") as directory:
        work = Path(directory)
        for name, test, old, new in mutations:
            project = work / name
            project.mkdir()
            for folder in ["src", "include", "tests", "semantics"]:
                shutil.copytree(ROOT / folder, project / folder)
            exercise(project, test, args.cutils.resolve(), args.cerberus, False)
            path = project / "src/cgrep.c"
            text = path.read_text()
            if text.count(old) != 1:
                raise RuntimeError("stale mutation: " + name)
            path.write_text(text.replace(old, new))
            # Compile failures never count as kills; mutation warnings are allowed.
            exercise(project, test, args.cutils.resolve(), args.cerberus, True)
            print("PASS compiled mutant rejected natively and by Cerberus:", name, flush=True)


if __name__ == "__main__":
    main()
