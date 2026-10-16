"""Differential test: cgrep against ripgrep on a generated corpus.

    python3 -I tests/difftest.py CGREP [--cases N] [--seed S]

Generates files (a small alphabet, so matches are common; empty lines, CRLF,
missing final newlines, NUL bytes near the start of some files) and
patterns (substrings of the corpus, single bytes, absent strings, the empty
pattern), then runs both tools on one file, several files, -n, -v, -c, -l,
-i, -w (some clustered), and standard input. The corpus is ASCII, where
cgrep's ASCII-only -i folding and byte-class -w words are exact. stdout and the exit status must be identical; stderr must be empty
in both or non-empty in both. The reference uses --no-mmap because cgrep
streams. Long binary lines, BOM/encoding conversion, and directories are
outside this generated corpus. Four larger text inputs exercise refills.
"""

import argparse
import os
import random
import subprocess
import sys
import tempfile
from pathlib import Path

ALPHABET = b"ab c\tx-.*[]$^?+|()AB_9"


def make_file(rng: random.Random) -> bytes:
    lines = []
    for _ in range(rng.randint(0, 40)):
        line = bytes(rng.choice(ALPHABET) for _ in range(rng.randint(0, 30)))
        lines.append(line + (b"\r" if rng.random() < 0.1 else b""))
    data = b"\n".join(lines)
    if lines and rng.random() < 0.8:
        data += b"\n"
    if data and rng.random() < 0.1:
        at = rng.randint(0, len(data))
        data = data[:at] + b"\0" + data[at:]
    return data


def make_pattern(rng: random.Random, files: list) -> bytes:
    roll = rng.random()
    if roll < 0.05:
        return b""
    if roll < 0.2:
        return bytes([rng.choice(ALPHABET)])
    if roll < 0.3:
        return b"zz" + bytes(rng.choice(ALPHABET) for _ in range(rng.randint(0, 3)))
    data = rng.choice(files)
    if roll < 0.4 and data and not data.endswith(b"\n"):
        # The file's last bytes: a match that ends exactly at the input's end.
        tail = data.split(b"\n")[-1].replace(b"\0", b"")
        if tail:
            return tail[-rng.randint(1, min(4, len(tail))):]
    text = data.replace(b"\n", b"").replace(b"\0", b"")
    if not text:
        return b"a"
    at = rng.randrange(len(text))
    return text[at:at + rng.randint(1, 6)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("cgrep")
    ap.add_argument("--cases", type=int, default=400)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--jobs", type=int, choices=range(1, 9), default=1,
                    help="cgrep worker count; reference remains ordered rg -j1")
    args = ap.parse_args()
    cgrep = str(Path(args.cgrep).resolve())
    rng = random.Random(args.seed)
    env = dict(os.environ, RIPGREP_CONFIG_PATH="")
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        names = []
        files = []
        for i in range(12):
            data = make_file(rng)
            name = f"f{i}.txt"
            (Path(tmp) / name).write_bytes(data)
            names.append(name)
            files.append(data)
        for i, target in enumerate([65535, 65536, 65537, 131079]):
            block = make_file(rng).replace(b"\0", b"x") or b"a\n"
            data = (block * (target // len(block) + 1))[:target]
            name = f"large{i}.txt"
            (Path(tmp) / name).write_bytes(data)
            names.append(name)
            files.append(data)
        for case in range(args.cases):
            pattern = make_pattern(rng, files)
            numbers = ["-n"] if rng.random() < 0.5 else []
            # Selection and summary flags, sometimes clustered with -n.
            roll = rng.random()
            if roll < 0.12:
                numbers += ["-v"]
            elif roll < 0.2:
                numbers += ["-c"]
            elif roll < 0.28:
                numbers += ["-l"]
            elif roll < 0.33:
                numbers += [rng.choice(["-vc", "-vl", "-cl", "-lc"])]
            elif roll < 0.36 and numbers:
                numbers = [rng.choice(["-nv", "-vn", "-nc", "-ln"])]
            # Case folding (with the pattern's case scrambled) and words.
            if rng.random() < 0.15:
                numbers += ["-i"]
                pattern = bytes(c ^ 32 if chr(c).isalpha() and rng.random() < 0.5 else c for c in pattern)
            if rng.random() < 0.15:
                numbers += [rng.choice(["-w", "-wv", "-wc", "-iw"])]
            shape = rng.random()
            stdin = b""
            if shape < 0.2:
                paths = ["-"] if rng.random() < 0.5 else []
                stdin = rng.choice(files)
            elif shape < 0.6:
                paths = [rng.choice(names)]
            else:
                paths = rng.sample(names, rng.randint(2, 4))
                if rng.random() < 0.1:
                    paths.append("missing.txt")
            tail = numbers + ["--", pattern.decode("latin-1")] + paths
            # -j1: one thread searches the paths in argument order, as cgrep
            # does; with threads ripgrep's output order varies.
            want = subprocess.run(["rg", "--no-config", "--no-mmap", "-F", "-j1"] + tail, input=stdin, capture_output=True,
                                  timeout=30, cwd=tmp, env=env)
            got = subprocess.run([cgrep, f"-j{args.jobs}"] + tail, input=stdin, capture_output=True, timeout=30, cwd=tmp)
            same = (want.returncode == got.returncode and want.stdout == got.stdout
                    and bool(want.stderr) == bool(got.stderr))
            if not same:
                failures += 1
                if failures <= 5:
                    print(f"case {case}: {tail!r} stdin={len(stdin)} bytes")
                    print(f"  rg    exit {want.returncode}: {want.stdout[:200]!r} err={want.stderr[:120]!r}")
                    print(f"  cgrep exit {got.returncode}: {got.stdout[:200]!r} err={got.stderr[:120]!r}")
    print(f"difftest: {args.cases - failures}/{args.cases} cases identical, cgrep -j{args.jobs} ordered")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
