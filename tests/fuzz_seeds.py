"""Generate/check reproducible libFuzzer startup corpora, including large refills."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def framing(pattern, payload, flags=195, failure=0):
    assert len(pattern) <= 32
    data = bytes([len(pattern), flags, failure]) + pattern + payload
    assert 3 <= len(data) <= 262144
    return data


def seeds():
    return {
        "fuzz_find": {
            "empty": b"\0\0",
            "exact-end": bytes([2, 0]) + b"ab" + b"aab",
            "overlap": bytes([5, 0]) + b"ababa" + b"bababababa",
            "binary": bytes([2, 0, 0, 255, 233, 0, 255]),
        },
        "fuzz_stream": {
            "bare": framing(b"alpha", b"alpha\nalpha", 0),
            "numbers": framing(b"alpha", b"no\nalpha\n\nalpha", 1),
            "names": framing(b"alpha", b"alpha\n", 2),
            "empty-pattern": framing(b"", b"\n\n\n\0", 195),
            "nul-same-delivery": framing(b"alpha", b"alpha\n\0tail"),
            "nul-after-output": framing(b"a", b"a\nx\0a"),
            "binary-split": framing(b"alpha", b"\0alpha", 0),
            "read-error": framing(b"a", b"a\nunfinished", 4, 2),
            "error-at-eof": framing(b"alpha", b"alpha", 0, 6),
            "grow-matching-line": framing(b"alpha", b"x" * 65534 + b"alpha\n"),
            "grow-unterminated-absent": framing(b"alpha", b"x" * 131075),
            "second-refill-boundary": framing(b"alpha", b"\n" * 131072 + b"alpha\n", 131),
            "third-refill-boundary": framing(b"alpha", b"\n" * 196608 + b"alpha\n"),
            "late-first-nul": framing(b"alpha", b"\n" * 65536 + b"\0\0alpha"),
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--write", action="store_true")
    action.add_argument("--check", action="store_true")
    args = parser.parse_args()
    for harness, corpus in seeds().items():
        directory = ROOT / "fuzz" / (harness + ".seeds")
        if args.write:
            directory.mkdir(parents=True, exist_ok=True)
        for name, data in corpus.items():
            path = directory / name
            if args.write:
                path.write_bytes(data)
            elif not path.is_file() or path.read_bytes() != data:
                raise RuntimeError(f"missing or stale generated seed: {path}")
        if {p.name for p in directory.iterdir()} != set(corpus):
            raise RuntimeError(f"unexpected files in generated corpus: {directory}")
        print(f"{harness}: {len(corpus)} seeds, largest {max(map(len, corpus.values()))} bytes")


if __name__ == "__main__":
    main()
