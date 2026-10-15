"""Warm explicit-file benchmarks: cgrep, GNU grep, and ripgrep thread settings.

Validate exact per-file line bytes/order, allowing different file ordering
only for ripgrep. Timed runs write complete output to real temporary files. Traversal and cold I/O are not
measured. Run under an outer memory cap; one search process runs at a time.
"""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import platform
import random
import resource
import shutil
import statistics
import subprocess
import tempfile
import time


def reference_output(name, data, pattern, names, numbers):
    chunks = []
    lines = data.split(b"\n")
    if not data or data.endswith(b"\n"):
        lines.pop()
    for number, body in enumerate(lines, 1):
        if pattern in body:
            prefix = (name.encode() + b":" if names else b"")
            if numbers:
                prefix += str(number).encode() + b":"
            chunks.append(prefix + body + b"\n")
    return b"".join(chunks)


def validate_output(result, expected, file_order, parallel):
    status = 0 if any(expected.values()) else 1
    if result.returncode != status or result.stderr:
        raise RuntimeError(f"search status/stderr mismatch: {result!r}")
    if not parallel or len(file_order) == 1:
        if result.stdout != b"".join(expected[name] for name in file_order):
            raise RuntimeError("exact output or serial file ordering mismatch")
        return
    if result.stdout and not result.stdout.endswith(b"\n"):
        raise RuntimeError("unterminated output line")
    actual = {name: bytearray() for name in file_order}
    for body in result.stdout.split(b"\n")[:-1]:
        line = body + b"\n"
        name = line.split(b":", 1)[0].decode("ascii")
        if name not in actual:
            raise RuntimeError("unexpected output path: " + name)
        actual[name].extend(line)
    if any(actual[name] != expected[name] for name in file_order):
        raise RuntimeError("per-file output bytes, completeness, or line ordering mismatch")


def commands(binary, pattern, paths, numbers, gnu_grep, baseline=None, cgrep_threads=False):
    numbering = ["-n"] if numbers else []
    result = {"cgrep": [binary, *numbering, "--", pattern, *paths],
              "gnu-grep": [gnu_grep, "--color=never", "-F", *numbering, "--", pattern, *paths]}
    if cgrep_threads:
        for threads in [1, 2, 4, 8]:
            result[f"cgrep-j{threads}"] = [binary, *numbering, f"-j{threads}", "--", pattern, *paths]
    common = ["rg", "--no-config", "--no-heading", "--color=never", "-F", *numbering]
    for threads in [1, 2, 4, 8]:
        result[f"rg-j{threads}"] = [*common, f"-j{threads}", "--", pattern, *paths]
    result["rg-default"] = [*common, "--", pattern, *paths]
    if baseline:
        result["baseline"] = [baseline, *numbering, "--", pattern, *paths]
        if cgrep_threads:
            for threads in [1, 2, 4, 8]:
                result[f"baseline-j{threads}"] = [baseline, *numbering, f"-j{threads}", "--", pattern, *paths]
    return result


MIXED_WORDS = (b"alpha beta gamma delta error warning info debug kernel mutex session request "
               b"handler buffer cache index token timeout retry value the of and to in is for "
               b"connection established closed user admin config server client update").split()


def mixed_text(size, seed=7):
    """Deterministic varied log-like lines. Common bytes make false candidates."""
    rng = random.Random(seed)
    lines, used = [], 0
    while True:
        words = [rng.choice(MIXED_WORDS) for _ in range(rng.randint(5, 14))]
        if rng.random() < 0.5:
            words.insert(rng.randrange(len(words) + 1), str(rng.randrange(1000)).encode())
        line = b" ".join(words) + b"\n"
        if used + len(line) > size:
            return b"".join(lines)
        lines.append(line)
        used += len(line)


def make_case(root, name, count, per_file, line, pattern, tail, numbers):
    directory = root / name
    directory.mkdir()
    data = line * (per_file // len(line)) + tail
    expected = {}
    paths = []
    fingerprint = hashlib.sha256()
    for i in range(count):
        path = f"f{i:04d}.txt"
        (directory / path).write_bytes(data)
        paths.append(path)
        expected[path] = reference_output(path, data, pattern.encode(), count > 1, numbers)
        fingerprint.update(path.encode())
        fingerprint.update(hashlib.sha256(data).digest())
    return directory, paths, expected, len(data) * count, fingerprint.hexdigest()


def measure(binary, directory, paths, expected, pattern, repeats, rng, numbers, gnu_grep, locale, baseline=None, cgrep_threads=False):
    variants = commands(binary, pattern, paths, numbers, gnu_grep, baseline, cgrep_threads)
    env = dict(os.environ, RIPGREP_CONFIG_PATH="", LC_ALL=locale)
    expected_length = sum(map(len, expected.values()))
    for name, command in variants.items():
        result = subprocess.run(command, cwd=directory, env=env, capture_output=True, timeout=60)
        validate_output(result, expected, paths, name.startswith("rg-"))
    samples = {name: [] for name in variants}
    cpu = {name: [] for name in variants}
    status = 0 if any(expected.values()) else 1
    for _ in range(repeats):
        order = list(variants)
        rng.shuffle(order)
        for name in order:
            before = resource.getrusage(resource.RUSAGE_CHILDREN)
            # A real sink prevents /dev/null shortcuts from avoiding printing
            # or remaining searches. Every tool must emit the complete result.
            with tempfile.TemporaryFile(dir=directory) as sink:
                started = time.perf_counter()
                result = subprocess.run(variants[name], cwd=directory, env=env,
                                        stdout=sink, stderr=subprocess.PIPE, timeout=60)
                elapsed = time.perf_counter() - started
                if sink.tell() != expected_length:
                    raise RuntimeError(f"{name}: timed output length mismatch")
                # Validate every timed byte after stopping the wall timer.
                # Same-length corruption must not pass between warm checks.
                sink.seek(0)
                validate_output(subprocess.CompletedProcess(variants[name], result.returncode,
                                                            sink.read(), result.stderr),
                                expected, paths, name.startswith("rg-"))
            after = resource.getrusage(resource.RUSAGE_CHILDREN)
            if result.returncode != status or result.stderr:
                raise RuntimeError(f"{name}: timed search failed: {result!r}")
            samples[name].append(elapsed)
            cpu[name].append(after.ru_utime + after.ru_stime - before.ru_utime - before.ru_stime)
    # Peak RSS is a separate diagnostic invocation, not wrapper overhead added
    # to the latency measurements. Revalidate bytes after the timed rounds.
    rss = {}
    for name, command in variants.items():
        result = subprocess.run(command, cwd=directory, env=env, capture_output=True, timeout=60)
        validate_output(result, expected, paths, name.startswith("rg-"))
        with tempfile.NamedTemporaryFile(prefix="rss-", dir=directory) as stat, tempfile.TemporaryFile(dir=directory) as sink:
            result = subprocess.run(["/usr/bin/time", "-q", "-f", "%M", "-o", stat.name, *command],
                                    cwd=directory, env=env, stdout=sink,
                                    stderr=subprocess.PIPE, timeout=60)
            if result.returncode != status or result.stderr:
                raise RuntimeError("RSS diagnostic search failed")
            rss[name] = int(Path(stat.name).read_text().strip())
    medians = {name: statistics.median(values) for name, values in samples.items()}
    return {"seconds": medians, "samples": samples,
            "cpu_seconds": {name: statistics.median(values) for name, values in cpu.items()},
            "cpu_samples": cpu,
            "peak_rss_kib": rss,
            "rg_speedup_vs_j1": {name: medians["rg-j1"] / medians[name]
                                 for name in variants if name.startswith("rg-")},
            "ratio_cgrep_to_tool": {name: medians["cgrep"] / medians[name]
                                   for name in variants if name != "cgrep"}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cgrep")
    parser.add_argument("--mib", type=int, default=64)
    parser.add_argument("--repeats", type=int, default=7)
    parser.add_argument("--case", action="append", help="restrict to a named workload; repeat to select several")
    parser.add_argument("--small-files", type=int, default=1024)
    parser.add_argument("--medium-files", type=int, default=16)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--numbers", action="store_true", help="request line numbers for all tools")
    parser.add_argument("--cgrep-threads", action="store_true", help="also compare cgrep -j1/-j2/-j4/-j8, with ordered output required")
    parser.add_argument("--baseline", type=Path, help="previous cgrep executable for randomized paired comparison")
    parser.add_argument("--gnu-grep", default=shutil.which("grep"), help="GNU grep executable")
    parser.add_argument("--locale", default="C", help="locale for every tool; default byte-oriented C")
    args = parser.parse_args()
    if min(args.mib, args.repeats, args.small_files, args.medium_files) < 1:
        parser.error("sizes and repetitions must be positive")
    binary = str(Path(args.cgrep).resolve())
    if not args.gnu_grep:
        parser.error("GNU grep is required")
    gnu_version = subprocess.check_output([args.gnu_grep, "--version"], text=True).splitlines()[0]
    if "GNU grep" not in gnu_version:
        parser.error("--gnu-grep must select GNU grep")
    total = args.mib * 1024 * 1024
    ordinary = b"alpha beta gamma delta\n"
    specs = [
        ("single-large-absent", 1, total, ordinary, "NEEDLE42", b""),
        ("medium-absent", args.medium_files, total // args.medium_files, ordinary, "NEEDLE42", b""),
        ("medium-late-match", args.medium_files, total // args.medium_files, ordinary, "NEEDLE42", b"NEEDLE42\n"),
        ("medium-uniform-stress", args.medium_files, total // args.medium_files, b"a" * 31 + b"\n", "a" * 32, b""),
        ("small-absent", args.small_files, 16384, ordinary, "NEEDLE42", b""),
        ("small-late-match", args.small_files, 16384, ordinary, "NEEDLE42", b"NEEDLE42\n"),
        ("matching-output-heavy", args.medium_files, 524288, ordinary, "alpha", b""),
    ]
    # Varied text reaches candidate rejection, which the uniform line above
    # never does. Each block is generated once and fills its file.
    medium = total // args.medium_files
    specs += [
        ("mixed-heavy", args.medium_files, medium, mixed_text(medium), "error", b""),
        ("mixed-common-anchor", args.medium_files, medium, mixed_text(medium), "e 5", b""),
        ("mixed-rare-absent", 1, total, mixed_text(total), "zebrafish", b""),
    ]
    if args.case:
        unknown = set(args.case) - {case[0] for case in specs}
        if unknown:
            parser.error("unknown workload: " + ", ".join(sorted(unknown)))
        specs = [case for case in specs if case[0] in args.case]
    report = {"schema": "cgrep.parallel-benchmark/1", "started_utc": datetime.now(timezone.utc).isoformat(), "cache": "warm", "output": "complete output to temporary regular files",
              "selection": "same explicit file arguments; no directory traversal", "repeats": args.repeats,
              "numbers": args.numbers, "cgrep_threads": args.cgrep_threads, "locale": args.locale, "gnu_grep": gnu_version,
              "baseline_sha256": hashlib.sha256(args.baseline.read_bytes()).hexdigest() if args.baseline else None,
              "ripgrep": subprocess.check_output(["rg", "--version"], text=True).splitlines()[0],
              "cgrep_sha256": hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
              "platform": platform.platform(), "logical_cpus": os.cpu_count(),
              "affinity_cpus": sorted(os.sched_getaffinity(0)), "cases": []}
    rng = random.Random(42)
    with tempfile.TemporaryDirectory(prefix="cgrep-parallel-benchmark-") as work:
        for name, count, size, line, pattern, tail in specs:
            directory, paths, expected, size, fingerprint = make_case(Path(work), name, count, size, line, pattern, tail, args.numbers)
            result = measure(binary, directory, paths, expected, pattern, args.repeats, rng, args.numbers, args.gnu_grep, args.locale,
                             str(args.baseline.resolve()) if args.baseline else None, args.cgrep_threads)
            report["cases"].append({"name": name, "files": count, "bytes": size,
                                     "input_sha256": fingerprint,
                                     "output_bytes": sum(map(len, expected.values())), **result})
    rendered = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
