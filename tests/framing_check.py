"""Replay seeded framing cases and prove exact-output oracle sensitivity."""
import argparse
import os
from pathlib import Path
import resource
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def build(project, cutils, binary, sanitize=False):
    compiler = "clang" if sanitize else os.environ.get("CC", "cc")
    flags = ["-std=c99", "-g", "-UNDEBUG", "-Wall", "-Wextra", "-Werror"]
    flags += (["-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
              if sanitize else ["-O2"])
    sources = list((project / "src").glob("*.c")) + list((cutils / "src").glob("*.c"))
    subprocess.run([*shlex.split(compiler), *flags, "-I" + str(project / "include"),
                    "-I" + str(cutils / "include"),
                    str(project / "tests/fixtures/framing_driver.c"),
                    *map(str, sources), "-o", str(binary)], check=True, timeout=90)


def run(binary, fixtures, cwd, reject=False):
    env = dict(os.environ, TMPDIR=str(cwd), ASAN_OPTIONS="detect_leaks=1:abort_on_error=1")
    result = subprocess.run([str(binary), *map(str, fixtures)], env=env, cwd=cwd,
                            capture_output=True, timeout=60)
    if reject:
        if result.returncode != -6 or b"reference_check_output" not in result.stderr:
            raise RuntimeError(f"mutant not rejected by output oracle: {result!r}")
    elif result.returncode != 0 or b"framing replay: PASS" not in result.stdout:
        raise RuntimeError(f"framing baseline failed: {result!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cutils", type=Path, default=ROOT.parent / "c-utils")
    parser.add_argument("--sensitivity", action="store_true")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    cutils = args.cutils.resolve()
    fixtures = sorted((ROOT / "fuzz/fuzz_stream.seeds").iterdir())
    if not fixtures:
        raise RuntimeError("framing seeds are required")
    with tempfile.TemporaryDirectory(prefix="cgrep-framing-check-") as directory:
        work = Path(directory)
        for sanitize in [False, True]:
            binary = work / ("sanitized" if sanitize else "native")
            build(ROOT, cutils, binary, sanitize)
            run(binary, fixtures, work)
            print(f"PASS framing replay: {len(fixtures)} fixtures + known outputs, "
                  f"{'ASan/UBSan/LSan' if sanitize else 'native'}", flush=True)
        if args.sensitivity:
            for name, old, new in [
                ("missing-newline", "        fputc('\\n', s->out);", "        /* seeded missing newline */"),
                ("missing-inline-newline", "            output_len++;", "            output_len += 0;"),
                ("wrong-number", 'number_prefix(number + sizeof number, seen + 1);',
                 'number_prefix(number + sizeof number, seen + 2);'),
                ("wrong-nul-offset", "nul_offset = read_base + (unsigned long)(nul - destination);",
                 "nul_offset = read_base + (unsigned long)(nul - destination) + 1;")
            ]:
                project = work / name
                project.mkdir()
                for directory in ["src", "include", "fuzz", "tests"]:
                    shutil.copytree(ROOT / directory, project / directory)
                path = project / "src/cgrep.c"
                text = path.read_text()
                if text.count(old) != 1:
                    raise RuntimeError(f"stale mutation location: {name}")
                path.write_text(text.replace(old, new))
                binary = project / "replay"
                build(project, cutils, binary, True)
                run(binary, fixtures, project, reject=True)
                print(f"PASS compiled output mutant rejected: {name}", flush=True)


if __name__ == "__main__":
    main()
