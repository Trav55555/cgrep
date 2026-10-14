"""Explicit-file mixed/uneven/repository searches with file or pipe output.

Run sequentially under the acceptance memory scope. Cache advice is not a
physical-cold-I/O proof. Never changes global caches or the source repository.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import platform
import random
import resource
import statistics
import subprocess
import tempfile
import time

spec = importlib.util.spec_from_file_location("parallel_benchmark", Path(__file__).with_name("benchmark_parallel.py"))
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)
commands = benchmark.commands
reference_output = benchmark.reference_output
validate_output = benchmark.validate_output


def corpus(kind, mib, repository):
    total = mib * 1024 * 1024
    if kind == "repository":
        names = subprocess.check_output(["git", "-C", str(repository), "ls-files", "-z"]).split(b"\0")
        rows, excluded, used = [], [], 0
        for raw in names:
            if not raw:
                continue
            name = os.fsdecode(raw)
            path = repository / name
            if not path.is_file() or path.is_symlink() or path.stat().st_size > 1048576:
                excluded.append({"path": name, "reason": "non-file, symlink, or exceeds 1 MiB"})
                continue
            data = path.read_bytes()
            if b"\0" in data or data.startswith((b"\xef\xbb\xbf", b"\xff\xfe", b"\xfe\xff")):
                excluded.append({"path": name, "reason": "binary or encoding/BOM outside supported comparison"})
                continue
            if used + len(data) > total:
                excluded.append({"path": name, "reason": "corpus budget"})
                continue
            rows.append((name, data))
            used += len(data)
        if not rows:
            raise ValueError("repository produced no supported text files")
        return rows, excluded
    rows = []
    if kind == "uneven":
        sizes = [total // 2, total // 4, total // 8] + [total // 104] * 13
    else:
        sizes = [0, 128, 1024, 16384, total // 32, total // 64] * 16
    shapes = [b"alpha beta gamma delta\n", b"x" * 4095 + b"\n", b"needle42\r\n",
              b"tab\tvertical\vreturn\r text\n", b"caf\xc3\xa9 source text\n"]
    for i, size in enumerate(sizes):
        line = shapes[i % len(shapes)]
        data = (line * (size // len(line) + 1))[:size]
        if i % 7 == 0:
            data += b"tail needle42"
        rows.append((f"generated-{i}", data))
    return rows, []


def prepare(directory, rows, pattern, numbers):
    names, expected, manifest = [], {}, []
    fingerprint = hashlib.sha256()
    for i, (source, data) in enumerate(rows):
        name = f"f{i:05d}.txt"
        (directory / name).write_bytes(data)
        names.append(name)
        expected[name] = reference_output(name, data, pattern.encode(), len(rows) > 1, numbers)
        digest = hashlib.sha256(data).hexdigest()
        manifest.append({"path": name, "source": source, "bytes": len(data), "sha256": digest})
        fingerprint.update(name.encode() + bytes.fromhex(digest))
    return names, expected, manifest, fingerprint.hexdigest()


def advise(directory, names):
    if not hasattr(os, "posix_fadvise") or not hasattr(os, "POSIX_FADV_DONTNEED"):
        raise RuntimeError("required targeted file-cache advice unavailable")
    for name in names:
        with (directory / name).open("rb") as stream:
            os.fsync(stream.fileno())
            os.posix_fadvise(stream.fileno(), 0, 0, os.POSIX_FADV_DONTNEED)


def invocation(command, directory, env, sink_kind, expected, names, parallel):
    before = resource.getrusage(resource.RUSAGE_CHILDREN)
    if sink_kind == "pipe":
        start = time.perf_counter()
        result = subprocess.run(command, cwd=directory, env=env, capture_output=True, timeout=90)
        elapsed = time.perf_counter() - start
    else:
        with tempfile.TemporaryFile(dir=directory) as sink:
            start = time.perf_counter()
            child = subprocess.run(command, cwd=directory, env=env, stdout=sink,
                                   stderr=subprocess.PIPE, timeout=90)
            elapsed = time.perf_counter() - start
            sink.seek(0)
            result = subprocess.CompletedProcess(command, child.returncode, sink.read(), child.stderr)
    after = resource.getrusage(resource.RUSAGE_CHILDREN)
    validate_output(result, expected, names, parallel)
    cpu = after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime
    return elapsed, cpu


def measure(binary, baseline, directory, names, expected, pattern, args):
    variants = commands(binary, pattern, names, args.numbers, args.gnu_grep, baseline, True)
    env = dict(os.environ, LC_ALL="C", RIPGREP_CONFIG_PATH="")
    # Every invocation checks every output byte, including timed file/pipe runs.
    samples = {name: [] for name in variants}
    cpu_samples = {name: [] for name in variants}
    rng = random.Random(42)
    for _ in range(args.repeats):
        order = list(variants)
        rng.shuffle(order)
        for name in order:
            if args.cache == "advise-cold":
                advise(directory, names)
            elapsed, cpu = invocation(variants[name], directory, env, args.sink,
                                      expected, names, name.startswith("rg-"))
            samples[name].append(elapsed)
            cpu_samples[name].append(cpu)
    return {"commands": variants, "samples": samples, "cpu_samples": cpu_samples,
            "seconds": {name: statistics.median(v) for name, v in samples.items()},
            "cpu_seconds": {name: statistics.median(v) for name, v in cpu_samples.items()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cgrep", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--repository", type=Path)
    parser.add_argument("--work-root", type=Path, required=True, help="explicit filesystem for generated copies and file sinks")
    parser.add_argument("--case", action="append", choices=["mixed", "uneven", "repository"])
    parser.add_argument("--mib", type=int, default=16)
    parser.add_argument("--repeats", type=int, default=7)
    parser.add_argument("--numbers", action="store_true")
    parser.add_argument("--pattern", default="needle42")
    parser.add_argument("--cache", choices=["warm", "advise-cold"], default="warm")
    parser.add_argument("--sink", choices=["file", "pipe"], default="file")
    parser.add_argument("--gnu-grep", default="grep")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not 1 <= args.mib <= 64 or args.repeats < 1 or "\n" in args.pattern or "\0" in args.pattern:
        parser.error("budget must be 1..64 MiB; repeats positive; literal must exclude LF/NUL")
    cases = args.case or ["mixed", "uneven"] + (["repository"] if args.repository else [])
    if "repository" in cases and not args.repository:
        parser.error("repository case requires --repository")
    args.work_root.mkdir(parents=True, exist_ok=True)
    binary = str(args.cgrep.resolve())
    baseline = str(args.baseline.resolve()) if args.baseline else None
    gnu_version = subprocess.check_output([args.gnu_grep, "--version"], text=True).splitlines()[0]
    if "GNU grep" not in gnu_version:
        parser.error("GNU grep required")
    fs = subprocess.check_output(["findmnt", "-T", str(args.work_root), "-n", "-o", "FSTYPE"], text=True).strip()
    report = {"schema": "cgrep.realistic-benchmark/0", "started_utc": datetime.now(timezone.utc).isoformat(),
              "cgrep_sha256": hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
              "baseline_sha256": hashlib.sha256(Path(baseline).read_bytes()).hexdigest() if baseline else None,
              "cache": args.cache, "cache_limit": "file-specific advice only; not proof of cold physical I/O",
              "filesystem": fs, "work_root": str(args.work_root.resolve()), "sink": args.sink,
              "sink_limit": "pipe output fully collected by parent; collector CPU included in wall time, not child CPU",
              "platform": platform.platform(), "load_average": os.getloadavg(),
              "selection": "same explicit generated/copied file arguments; no tool directory traversal", "numbers": args.numbers,
              "repeats": args.repeats, "gnu_grep": gnu_version,
              "ripgrep": subprocess.check_output(["rg", "--version"], text=True).splitlines()[0], "cases": []}
    if args.repository:
        args.repository = args.repository.resolve()
        report["repository"] = str(args.repository)
        report["repository_head"] = subprocess.check_output(["git", "-C", str(args.repository), "rev-parse", "HEAD"], text=True).strip()
        report["repository_dirty"] = bool(subprocess.check_output(["git", "-C", str(args.repository), "status", "--porcelain"]))
    with tempfile.TemporaryDirectory(prefix="cgrep-realistic-", dir=args.work_root) as temporary:
        for kind in cases:
            rows, excluded = corpus(kind, args.mib, args.repository)
            directory = Path(temporary) / kind
            directory.mkdir()
            names, expected, manifest, digest = prepare(directory, rows, args.pattern, args.numbers)
            if args.cache == "warm":
                for name in names:
                    (directory / name).read_bytes()
            result = measure(binary, baseline, directory, names, expected, args.pattern, args)
            report["cases"].append({"name": kind, "input_sha256": digest, "manifest": manifest,
                                    "excluded": excluded, "output_bytes": sum(map(len, expected.values())), **result})
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print("PASS realistic benchmark: every timed output/status validated;", args.output)


if __name__ == "__main__":
    main()
