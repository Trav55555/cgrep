"""Differential test of ignore files: cgrep against ripgrep on generated trees.

    python3 -I tests/ignore_difftest.py CGREP [--cases N] [--seed S] [--jobs J]

Each case builds a small tree under /var/tmp (disk, and outside any git
repository, unlike /tmp here) with random .gitignore, .ignore, .rgignore,
.git/info/exclude and global excludes files, sometimes a .git at the root
or deeper, and searches it from its root, a subdirectory or a parent, as a
path or from inside it. Both tools list matching files (-l) with HOME and
XDG_CONFIG_HOME pointing into the case, so only its own global file
applies. stdout and the exit status must match; stderr must be empty in
both or non-empty in both. The reference runs with --sort path.
"""

import argparse
import os
import random
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

NAMES = ["a", "b", "a.log", "b.txt", "x", "sub", "deep", ".h", "A.c", "a b"]
COMPONENTS = ["a", "b", "*", "?", "a*", "*.log", "*.txt", "[ab]", "[!a]*", "[a-c]", "**", "sub", "deep",
              "x", ".h", "A.c", "\\*", "a\\ b"]


def make_pattern(rng: random.Random) -> str:
    parts = [rng.choice(COMPONENTS) for _ in range(rng.randint(1, 3))]
    line = "/".join(parts)
    roll = rng.random()
    if roll < 0.15:
        line = "/" + line
    if rng.random() < 0.2:
        line += "/"
    if rng.random() < 0.2:
        line = "!" + line
    if rng.random() < 0.05:
        line = "# " + line
    if rng.random() < 0.05:
        line += "  "
    return line


def make_ignore(rng: random.Random) -> str:
    return "".join(make_pattern(rng) + "\n" for _ in range(rng.randint(1, 4)))


def make_tree(rng: random.Random, root: Path, depth: int, dirs: list) -> None:
    root.mkdir(exist_ok=True)
    dirs.append(root)
    for name in rng.sample(NAMES, rng.randint(1, 5)):
        path = root / name
        if depth < 3 and rng.random() < 0.35:
            make_tree(rng, path, depth + 1, dirs)
        else:
            path.write_bytes(b"alpha\n")
    for kind in [".gitignore", ".ignore", ".rgignore"]:
        if rng.random() < 0.3:
            (root / kind).write_text(make_ignore(rng))
    if depth > 0 and rng.random() < 0.08:
        (root / ".git").mkdir()


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("cgrep")
    ap.add_argument("--cases", type=int, default=300)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--jobs", type=int, choices=range(1, 33), default=1)
    ap.add_argument("--keep", type=Path, help="copy each failing case's tree here, for debugging")
    args = ap.parse_args()
    cgrep = str(Path(args.cgrep).resolve())
    # Repository detection looks for .git above the tree: a checkout above
    # /var/tmp would make every case a repository case.
    for parent in [Path("/var/tmp"), *Path("/var/tmp").parents]:
        if (parent / ".git").exists():
            raise SystemExit(f"ignore difftest: {parent}/.git exists, so /var/tmp is inside a repository")
    rng = random.Random(args.seed)
    failures = 0
    for case in range(args.cases):
        with tempfile.TemporaryDirectory(prefix="cgrep-ignore-", dir="/var/tmp") as tmp:
            base = Path(tmp)
            home = base / "home"
            (home / ".config" / "git").mkdir(parents=True)
            if rng.random() < 0.3:
                (home / ".config" / "git" / "ignore").write_text(make_ignore(rng))
            top = base / "top"
            top.mkdir()
            if rng.random() < 0.3:
                (top / ".gitignore").write_text(make_ignore(rng))
            if rng.random() < 0.3:
                (top / ".ignore").write_text(make_ignore(rng))
            dirs = []
            make_tree(rng, top / "r", 0, dirs)
            if rng.random() < 0.6:
                git = rng.choice([top, top / "r"])
                (git / ".git" / "info").mkdir(parents=True, exist_ok=True)
                if rng.random() < 0.4:
                    (git / ".git" / "info" / "exclude").write_text(make_ignore(rng))
            start = rng.choice(dirs)
            shape = rng.random()
            if shape < 0.4:
                cwd, target = top, str(start.relative_to(top))
            elif shape < 0.6:
                cwd, target = start, "."
            elif shape < 0.75:
                cwd, target = start.parent, str(start.relative_to(start.parent)) + "/"
            else:
                cwd, target = base, str(start)
            env = dict(os.environ, HOME=str(home), XDG_CONFIG_HOME="", RIPGREP_CONFIG_PATH="")
            flags = []
            if rng.random() < 0.5:
                flags += ["--hidden"]
            if rng.random() < 0.5:
                flags += ["--max-depth", str(rng.randrange(5))]
            tail = flags + ["-l", "--", "alpha", target]
            want = subprocess.run(["rg", "--no-config", "--no-mmap", "-F", "-j1", "--sort", "path"] + tail,
                                  cwd=cwd, capture_output=True, timeout=30, env=env, stdin=subprocess.DEVNULL)
            got = subprocess.run([cgrep, f"-j{args.jobs}"] + tail, cwd=cwd, capture_output=True, timeout=30,
                                 env=env, stdin=subprocess.DEVNULL)
            same = (want.returncode == got.returncode and want.stdout == got.stdout
                    and bool(want.stderr) == bool(got.stderr))
            if not same:
                failures += 1
                print(f"case {case}: cwd={cwd.relative_to(base)} target={target} flags={flags}")
                for f in sorted(base.rglob("*")):
                    if f.is_file() and (f.name in (".gitignore", ".ignore", ".rgignore", "exclude", "ignore")):
                        print(f"  {f.relative_to(base)}: {f.read_text()!r}")
                print(f"  rg    exit {want.returncode}: {want.stdout.decode(errors='replace')!r}")
                print(f"  cgrep exit {got.returncode}: {got.stdout.decode(errors='replace')!r}")
                if args.keep is not None:
                    shutil.copytree(base, args.keep / f"case{case}", symlinks=True)
    print(f"ignore difftest: {args.cases - failures}/{args.cases} cases identical, cgrep -j{args.jobs}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
