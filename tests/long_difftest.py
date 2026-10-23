"""Differential test of printing around a line longer than 8 MiB.

    python3 -I tests/long_difftest.py CGREP [--cases N] [--seed S]

Printing keeps whole lines, at most 8 MiB each. A longer line is read
through and skipped unless it is selected; a selected one cannot be
printed and is an error. Each case writes one file holding such a line,
with the pattern placed at window and limit boundaries, as a word or not,
in either case, between ordinary lines. When rg -F does not print the long
line, cgrep must give the same stdout, stderr and exit status; when it
does, cgrep must exit 2 with the line-limit error.
"""

import argparse
import os
import random
import subprocess
import sys
import tempfile

LIMIT = 8 * 1024 * 1024
FLAGS = [["-n"], ["-n", "-i"], ["-n", "-w"], ["-n", "-v"], [], ["-i", "-w"], ["-n", "-w", "-i"]]


def make_case(rng: random.Random):
    size = LIMIT + rng.choice([1, 5, 65536, LIMIT // 2, LIMIT + 3])
    line = bytearray(b"x" * size)
    if rng.random() < 0.6:
        at = rng.choice([0, 1, 65531, 65534, LIMIT - 5, LIMIT - 2, LIMIT, LIMIT + 1,
                         size - 5, rng.randrange(size - 5)])
        at = min(at, size - 5)
        line[at:at + 5] = rng.choice([b"alpha", b"ALPHA", b"Alpha"])
        if rng.random() < 0.5 and at > 0:
            line[at - 1] = ord(" ")
        if rng.random() < 0.5 and at + 5 < size:
            line[at + 5] = ord(" ")
    before = b"".join(rng.choice([b"alpha one\n", b"plain\n", b"ALPHA two\n"])
                      for _ in range(rng.randrange(3)))
    after = b"".join(rng.choice([b"alpha three\n", b"none\n", b"x alpha\n"])
                     for _ in range(rng.randrange(3)))
    end = b"" if rng.random() < 0.2 else b"\n"
    return before + bytes(line) + end + after, rng.choice(FLAGS)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("cgrep")
    ap.add_argument("--cases", type=int, default=30)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    rng = random.Random(args.seed)
    failed = 0
    selected = 0
    with tempfile.TemporaryDirectory() as scratch:
        path = os.path.join(scratch, "input")
        for case in range(args.cases):
            data, flags = make_case(rng)
            with open(path, "wb") as stream:
                stream.write(data)
            tail = flags + ["--", "alpha", path]
            got = subprocess.run([args.cgrep] + tail, capture_output=True)
            want = subprocess.run(["rg", "--no-config", "-F", "--no-mmap", "-j1"] + tail,
                                  capture_output=True)
            if any(len(line) > LIMIT for line in want.stdout.split(b"\n")):
                selected += 1
                ok = got.returncode == 2 and b"line exceeds 8 MiB" in got.stderr
            else:
                ok = (got.returncode, got.stdout, got.stderr) == \
                     (want.returncode, want.stdout, want.stderr)
            if not ok:
                failed += 1
                print(f"case {case}: {flags}, {len(data)} bytes")
                print(f"  cgrep exit {got.returncode}: {got.stdout[:80]!r} {got.stderr[:120]!r}")
                print(f"  rg    exit {want.returncode}: {want.stdout[:80]!r}")
    print(f"long_difftest: {args.cases - failed}/{args.cases} cases agree, "
          f"{selected} with the long line selected")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
