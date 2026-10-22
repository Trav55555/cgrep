"""Differential test of counting and listing across read windows.

    python3 -I tests/window_difftest.py CGREP [--cases N] [--seed S]

-c and -l stream an input in 64 KiB windows (scan_window) instead of
keeping lines. Each case writes one file sized near a window boundary,
mixing short lines, the pattern, runs of newlines and lines longer than a
window, so matches, word boundaries and line ends land on both sides of a
boundary. cgrep and rg -F must give the same stdout and exit status.
"""

import argparse
import os
import random
import subprocess
import sys
import tempfile

SIZES = [3, 4, 100, 65535, 65536, 65537, 65539, 131072, 131075, 200000]
FLAGS = [["-c"], ["-l"], ["-cw"], ["-lw"], ["-ci"], ["-cv"], ["-lv"], ["-cvw"]]
ALPHABET = b"ab_ \n"


def make_case(rng: random.Random):
    pattern = bytes(rng.choice(b"ab_ ") for _ in range(rng.randint(1, 5))).strip() or b"a"
    size = rng.choice(SIZES)
    parts = []
    total = 0
    while total < size:
        roll = rng.random()
        if roll < 0.02:
            chunk = pattern
        elif roll < 0.05:
            chunk = b"\n" * rng.randint(1, 3)
        elif roll < 0.06:
            chunk = b"x" * rng.randint(1000, 70000)
        else:
            chunk = bytes(rng.choice(ALPHABET) for _ in range(rng.randint(1, 40)))
        parts.append(chunk)
        total += len(chunk)
    data = b"".join(parts)[:size]
    if rng.random() < 0.5 and not data.endswith(b"\n"):
        data += b"\n"
    return pattern, data, rng.choice(FLAGS)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("cgrep")
    ap.add_argument("--cases", type=int, default=400)
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args()
    rng = random.Random(args.seed)
    failed = 0
    with tempfile.TemporaryDirectory() as scratch:
        path = os.path.join(scratch, "input")
        for case in range(args.cases):
            pattern, data, flags = make_case(rng)
            with open(path, "wb") as stream:
                stream.write(data)
            tail = flags + ["--", pattern, path]
            got = subprocess.run([args.cgrep] + tail, capture_output=True)
            want = subprocess.run(["rg", "--no-config", "-F", "-j1"] + tail, capture_output=True)
            if (got.returncode, got.stdout) != (want.returncode, want.stdout):
                failed += 1
                print(f"case {case}: {flags} {pattern!r}, {len(data)} bytes")
                print(f"  cgrep exit {got.returncode}: {got.stdout!r}")
                print(f"  rg    exit {want.returncode}: {want.stdout!r}")
    print(f"window_difftest: {args.cases - failed}/{args.cases} cases identical")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
