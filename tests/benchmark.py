"""Compare literal search on reproducible corpora; output JSON measurements."""
import argparse
import hashlib
import json
import os
import random
import statistics
import subprocess
import tempfile
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cgrep")
    parser.add_argument("--mib", type=int, default=8)
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--baseline", type=Path, help="optional previous cgrep binary")
    args = parser.parse_args()
    if args.mib < 1 or args.repeats < 1:
        parser.error("mib and repeats must be positive")
    binary = str(Path(args.cgrep).resolve())
    target = args.mib * 1024 * 1024
    line = b"alpha beta gamma delta\n"
    ordinary = line * (target // len(line))
    repetitive_line = b"a" * 128 + b"\n"
    misleading_line = b"b" * 128 + b"\n"
    uniform_line = b"a" * 31 + b"\n"
    two_hot_line = b"bc" * 64 + b"\n"
    three_hot_line = b"bcd" * 42 + b"\n"
    corpora = [
        ("absent", "NEEDLE42", ordinary, b"", 1),
        ("last-line", "NEEDLE42", ordinary + b"NEEDLE42\n", b"NEEDLE42\n", 0),
        ("repetitive", "a" * 31 + "b", repetitive_line * (target // len(repetitive_line)), b"", 1),
        ("misleading-anchor", "b" + "a" * 31, misleading_line * (target // len(misleading_line)), b"", 1),
        ("uniform", "a" * 32, uniform_line * (target // len(uniform_line)), b"", 1),
        ("two-hot-anchors", "bc" + "a" * 30, two_hot_line * (target // len(two_hot_line)), b"", 1),
        ("three-hot-anchors", "bcd" + "a" * 29, three_hot_line * (target // len(three_hot_line)), b"", 1),
    ]
    report = {"ripgrep": subprocess.check_output(["rg", "--version"], text=True).splitlines()[0],
              "repeats": args.repeats, "cache": "warm", "cases": []}
    rng = random.Random(42)
    with tempfile.TemporaryDirectory(prefix="cgrep-benchmark-") as directory:
        path = Path(directory) / "corpus.txt"
        for name, pattern, data, expected, status in corpora:
            path.write_bytes(data)
            commands = {"cgrep": [binary, "--", pattern, str(path)],
                        "rg": ["rg", "--no-config", "-F", "-j1", "--", pattern, str(path)]}
            if args.baseline:
                commands["baseline"] = [str(args.baseline.resolve()), "--", pattern, str(path)]
            samples = {tool: [] for tool in commands}
            for tool, command in commands.items():
                result = subprocess.run(command, capture_output=True, timeout=30)
                if (result.returncode, result.stdout, result.stderr) != (status, expected, b""):
                    raise RuntimeError(f"{name}: {tool} produced an incorrect result: {result!r}")
            for _ in range(args.repeats):
                order = list(commands)
                rng.shuffle(order)
                for tool in order:
                    started = time.perf_counter()
                    result = subprocess.run(commands[tool], stdout=subprocess.DEVNULL,
                                            stderr=subprocess.PIPE, timeout=30)
                    samples[tool].append(time.perf_counter() - started)
                    if result.returncode != status or result.stderr:
                        raise RuntimeError(f"{name}: {tool} failed during measurement")
            medians = {tool: statistics.median(values) for tool, values in samples.items()}
            report["cases"].append({"name": name, "bytes": len(data),
                                    "sha256": hashlib.sha256(data).hexdigest(),
                                    "seconds": medians, "samples": samples,
                                    "ratio_cgrep_to_rg": medians["cgrep"] / medians["rg"]})
    rendered = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
