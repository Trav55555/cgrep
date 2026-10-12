# cgrep

A ripgrep reimplementation in Dependable C, used to exercise c-utils with
real file I/O and search workloads. This first slice streams literal
searches over explicit files or standard input.

```sh
make                         # builds build/cgrep
./build/cgrep [-n] PATTERN [PATH...]
printf 'one\ntwo one\n' | ./build/cgrep -n one
./build/cgrep -- -needle file.txt
```

With no paths, or a path of `-`, cgrep reads standard input. Multiple
inputs get a path prefix; `-n` adds line numbers. The status is 0 for a
match, 1 for no matches, or 2 for a usage, read, close, allocation, resource
limit, or output error. An input failure does not prevent later paths from
being searched. An observed output failure stops later inputs. On POSIX,
a closed output pipe can terminate the CLI with SIGPIPE instead.

Patterns are literal. Compare streaming behavior with
`rg -F --no-config --no-mmap -j1` using piped output. Newline patterns are
rejected. Text output preserves CRLF and adds a newline to matching final
unterminated lines. BOM stripping and encoding conversion are not supported.

cgrep reuses a 64 KiB input window across files, growing it only for an
unfinished line. It prepares the literal once per command. `memchr`
filters candidates using the byte least repeated within the pattern, and
`memcmp` confirms them. That
choice is only a heuristic. Search inspects the first failed comparison
once and uses its mismatched expected byte to suggest a different filter.
If that byte is unchanged, it keeps the pattern-derived alternate. After
at most one switch and a bounded number of false candidates, search falls
back to Knuth-Morris-Pratt, or KMP. Its prefix table takes O(pattern
length) memory. Search work is O(input length + pattern length), including
preparation. Line counting runs only when `-n` requests it.

## Streaming limits and binary input

Text lines may contain at most 8 MiB before LF. Exactly-at-limit lines work
with LF or EOF; longer lines report an error. Geometric arena growth keeps
under 16 MiB of window storage plus metadata, with O(pattern length) table
storage and libc overhead in addition. This is not a total process-memory
limit. Tests exercise 64 MiB files and pipes under a 32 MiB address-space
limit with a small pattern.

Each refill is checked for NUL before its text is searched. After NUL
appears, cgrep prints one binary warning if any match has been found,
including a match from an earlier refill, then stops that input. Otherwise
it searches subsequent chunks with persistent KMP state and no further
window growth. Previously printed lines stay printed. The warning uses
the first NUL's offset in delivered bytes. mmap-based ripgrep can differ
on late NULs; refill boundaries can also differ for long lines.

Byte counters use `unsigned long`; input beyond `ULONG_MAX` delivered bytes
reports an error rather than wrapping. Borrowed stdin starts at its current
position, inherits its mode, and stays open with read-updated flags. Reads
use blocking stdio and may read ahead; immediate interactive output is not
promised. Partial output from
earlier windows remains visible after a later input failure.

Recursive traversal, regex, ignore rules, terminal formatting, and parallel
search are not implemented.

## Build and verification

Keep c-utils beside this checkout, or pass `CUTILS=/path/to/c-utils` to
make. For dcheck, set `DEPS` in `dcheck.conf` to the same dependency. Its
`bin/dcheck` lives in `~/projects/dependable-c-agent-toolchain` by default;
override `DCHECK` in make if needed.

```sh
make test                    # strict C89 unit tests
make difftest                # 400 generated comparisons against ripgrep
make iotest                  # native allocation and output failure checks
make check                   # compiler matrix, sanitizers, wasm, coverage, I/O
make mutate                  # isolated, bounded mutation runs
python3 -I tests/benchmark.py build/cgrep --repeats 7
```

The benchmark includes absent literals, a last-line match, repeated-prefix
failures, a misleading candidate byte, uniform patterns, and two hot
candidate bytes, including a three-hot-byte case. Pass
`--baseline /path/to/previous-cgrep` for a paired comparison. The benchmark
shuffles program order on every repetition. Fuzz harnesses cover byte
matching in `fuzz/fuzz_find.c` and short-read/error framing in
`fuzz/fuzz_stream.c`. `dcheck deep` runs them when the required tools exist;
see [verification.md](verification.md) for known deep-check tool failures.

The differential test compares stdout bytes and status, and requires both
tools to agree on whether stderr is empty. It exercises stdin, explicit
`-`, multiple paths, missing paths, empty and literal metacharacter
patterns, binary data, line boundaries, and larger text files around
refill sizes. Detailed error wording, BOM decoding, and long binary-line
refill differences are outside that generated comparison.

`make mutate` uses the installed c-verification mutation runner. Each
worker rebuilds its own copy, runs the unit tests in native, ASan/UBSan,
and MSan builds, and then runs 150 differential cases. Defaults are two
workers, 30 seconds per command, a 16 MiB output-file limit, a 768 MiB
native address-space limit, and a 512 MiB ASan RSS limit. The script sets
the core-file limit to zero. Use an outer memory limit for aggregate worker
memory; sanitizer builds need more virtual address space than the native
limit permits.

Leak detection belongs to `make check`; the mutation ASan run checks
bounds and UB. Override `JOBS`, `MUTATION_TIMEOUT`, or `MUTANTS` as needed.
The runner returns 1 if any mutation survives, including equivalent
mutations; their reasoning is recorded in [verification.md](verification.md).

## Next slices

1. Recursive traversal, hidden-file and binary defaults.
2. A regex parser and Thompson NFA.
3. A lazy DFA, prefilters, and benchmarks against ripgrep.
4. Ignore rules, including `.gitignore`.
5. A platform thread pool and parallel file search.

See [Runtime TODOs](TODO.md) for error contracts, resource limits, scoped
workers, bounded queues, cancellation, and concurrency verification.
