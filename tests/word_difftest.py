"""Differential test of -w beside non-ASCII text.

    python3 -I tests/word_difftest.py CGREP [--cases N] [--seed S]

-w needs a non-word character or a line end on each side of a match, with
the reference's Unicode word characters; bytes that are not valid UTF-8
count as word characters. Each case writes one file whose lines put the
pattern next to word and non-word characters of every UTF-8 length,
invalid and cut-off sequences, and line ends, some across the 64 KiB
window boundary that counting and listing stream through. cgrep and
rg -F must give the same stdout and exit status.
"""

import argparse
import os
import random
import subprocess
import sys
import tempfile

WINDOW = 65536
NEIGHBORS = [
    # Word characters: letters, marks, digits, connector, join control.
    "\u00e9", "\u00df", "\u65e5", "\u0436", "\u0301", "\u0663", "\u203f", "\u200d", "\u24b6", "\U0001d400", "\U00031350",
    # Non-word characters: punctuation, spaces, symbols, emoji.
    "\u3001", "\u00a0", "\u2014", "\u201c", "\u20ac", "\U0001f600", "\u3000", "\u2028", "-", " ", "\t", "\r", ".",
    # ASCII word characters.
    "a", "Z", "0", "_",
]
INVALID = [b"\xff", b"\x80", b"\xc0\x80", b"\xe3\x80", b"\xed\xa0\x80", b"\xf4\x90\x80\x80",
           b"\xf0\x9f\x98", b"\xc3"]
FLAGS = [["-wc"], ["-wl"], ["-wn"], ["-wcv"], ["-wci"], ["-wni"]]


def side(rng: random.Random) -> bytes:
    if rng.random() < 0.25:
        return rng.choice(INVALID)
    return "".join(rng.choice(NEIGHBORS) for _ in range(rng.randint(1, 2))).encode()


def make_case(rng: random.Random):
    pattern = rng.choice([b"main", b"ab", b"x", b"\xc3\xa9t\xc3\xa9", b"_a"])
    lines = []
    size = 0
    target = rng.choice([200, WINDOW - 40, WINDOW + 40, 2 * WINDOW + 7])
    while size < target:
        roll = rng.random()
        if roll < 0.6:
            line = side(rng) + pattern + side(rng)
        elif roll < 0.75:
            line = pattern + side(rng)
        elif roll < 0.9:
            line = side(rng) + pattern
        else:
            line = b"filler " * rng.randint(1, 30)
        lines.append(line)
        size += len(line) + 1
    data = b"\n".join(lines)
    if rng.random() < 0.7:
        data += b"\n"
    return pattern, data, rng.choice(FLAGS)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("cgrep")
    ap.add_argument("--cases", type=int, default=300)
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
            want = subprocess.run(["rg", "--no-config", "-F", "--no-mmap", "-j1"] + tail,
                                  capture_output=True)
            if (got.returncode, got.stdout) != (want.returncode, want.stdout):
                failed += 1
                print(f"case {case}: {flags} {pattern!r}, {len(data)} bytes")
                print(f"  cgrep exit {got.returncode}: {got.stdout[:160]!r}")
                print(f"  rg    exit {want.returncode}: {want.stdout[:160]!r}")
    print(f"word_difftest: {args.cases - failed}/{args.cases} cases identical")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
