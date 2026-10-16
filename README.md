# cgrep

A ripgrep reimplementation in Dependable C, used to exercise c-utils with
real file I/O and search workloads. This first slice streams literal
searches over explicit files or standard input.

```sh
make                         # builds build/cgrep
./build/cgrep [-cilnvw] [-j N] PATTERN [PATH...]
printf 'one\ntwo one\n' | ./build/cgrep -n one
./build/cgrep -vc one a.txt b.txt  # per-file counts of lines without "one"
./build/cgrep -- -needle file.txt
./build/cgrep --version       # cgrep 0.1.0 (<rev>, c-utils <rev>)
```

With no paths, or a path of `-`, cgrep reads standard input. A directory
argument is searched recursively, as `rg --sort path --no-ignore` does:
each directory's names in byte order, depth first, skipping names that
start with `.`, symbolic links (a linked argument is followed) and
non-regular files. `.gitignore`, `.ignore` and `.rgignore` files are not
yet honoured. A binary file found this way stops at the refill holding its
first NUL: earlier lines stay printed and are followed by a "stopped
searching binary file" warning, and `-c` drops the file. An unreadable
directory is reported and the walk continues. Multiple inputs, or any
directory argument, get a path prefix; `-n` adds line numbers. `-v` selects lines that
do not match. `-c` prints each input's count of selected lines (omitting
zero counts) and `-l` the name of each input with one (`<stdin>` for
standard input); the later of the two wins, and `-n` does not apply to
either. `-i` matches ASCII letters in either case and `-w` only matches
with no word character just before or after (ripgrep's half boundaries).
Two documented differences from ripgrep remain until the regex engine:
`-i` does not fold non-ASCII letters (`é` does not match `É`), and `-w`
treats every non-ASCII character as a word character, which is exact for
letters but not for non-ASCII punctuation such as `—`. Short flags cluster
(`-vn`, `-iwc`, `-cj4`). As in ripgrep, `-c` and `-l`
treat NUL bytes as line ends and keep no lines, so the 8 MiB line limit
does not apply to them; printing a binary input still stops at a warning.
The status is 0 when a line is selected, 1 when none is, or 2 for a usage,
read, close, allocation, resource limit, or output error. An input failure does not prevent later paths from
being searched. An observed output failure stops serial input or cancels
prefetched workers. On POSIX, a closed output pipe can terminate the CLI
with SIGPIPE instead.

Default execution is serial. `-j N` or `-jN`, for 1 through 8 workers,
opts into bounded regular-file parallelism on Linux/glibc 2.34 or newer.
Output and diagnostics retain argument order. Stdin stays serial, and a
path that turns out not to be a regular file is searched serially when its
turn comes. Unsupported targets stay serial. Worker `w` searches paths
`w`, `w + N`, and so on, each with its own input window and two 1 MiB output
slots, so it can search its next file while the coordinator writes the
previous one. Slots are allocated only for workers actually started. The pool
joins all started workers before returning. Regular-file reads can delay
cancellation, which workers check every eighth refill. Small matching files
can still be slower with workers; see [performance](docs/PERFORMANCE.md).

Patterns are literal. Compare streaming behavior with
`rg -F --no-config --no-mmap -j1` using piped output. Newline patterns are
rejected. Text output preserves CRLF and adds a newline to matching final
unterminated lines. BOM stripping and encoding conversion are not supported.

cgrep reuses a 64 KiB input window across files, growing it only for an
unfinished line. It prepares the literal once per command, choosing two
filter bytes with a static background byte-frequency rank (from the memchr
crate), not from the pattern's own bytes. A candidate must match both bytes
before `memcmp` confirms it. Search starts with libc `memchr` on the rarer
byte; on x86-64, once those hits prove dense, it switches to an SSE2 scan of
both bytes at once. Confirmation work is charged against the bytes skipped.
When it outruns them, search switches once per input to a byte that
actually mismatched, and after that runs Knuth-Morris-Pratt, or KMP, over
one bounded window before resuming the filter. Learned choices persist
across matches within an input. Uniform literals use a separate linear path:
a mismatching terminal byte skips a full window, and a failed comparison
skips through its rightmost blocking byte. Its prefix table takes O(pattern
length) memory. Search work is O(input length + pattern length), including
preparation.

Line counting runs only when `-n` requests it, lazily up to each printed
line. On x86-64 it counts 16 bytes per step with SSE2 compare masks. Other
targets use `memchr`, and after four nearby newlines count at most 1024
bytes using exact word-lane masks. Byte loads use `memcpy`; word counting is
enabled only for checked 32/64-bit unsigned-long representations with 8-bit
bytes. Four independent loads share a loop step, with up to 16 words per lane
reduction and no byte-lane carry. Other representations retain
character-only counting. Without names or numbers, adjacent matching lines
are written as one slice. The Linux CLI uses a 64 KiB stdout buffer for
non-TTY output only and glibc's unlocked stdio; caller-owned library streams
keep their existing setup. Build with `-DCGREP_NO_SIMD` to force the
portable code.

## Streaming limits and binary input

Text lines may contain at most 8 MiB before LF. Exactly-at-limit lines work
with LF or EOF; longer lines report an error. Geometric arena growth keeps
under 16 MiB of window storage plus metadata, with O(pattern length) table
storage and libc overhead in addition. This is a per-worker window bound, not a total process-memory
limit. Pattern storage is shared. Tests exercise serial 64 MiB files/pipes
and eight workers scanning 448 MiB under a 32 MiB address-space limit with
a small pattern and bounded-length lines.

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

Recursive traversal, regex, ignore rules, and terminal formatting are not
implemented.

## Build and verification

Keep c-utils beside this checkout, or pass `CUTILS=/path/to/c-utils` to
make. For dcheck, set `DEPS` in `dcheck.conf` to the same dependency. Its
`bin/dcheck` lives in `~/projects/dependable-c-agent-toolchain` by default;
override `DCHECK` in make if needed.

```sh
make verify                  # complete, resource-capped acceptance profile
make test                    # strict C89 unit tests
make difftest                # 400 generated comparisons against ripgrep
make iotest                  # native allocation and output failure checks
make check                   # compiler matrix, sanitizers, wasm, coverage, I/O
make mutate                  # isolated, bounded mutation runs
python3 -I tests/benchmark.py build/cgrep --repeats 7
python3 -I tests/benchmark_parallel.py build/cgrep --mib 64 --repeats 7
python3 -I tests/benchmark_parallel.py build/cgrep --mib 64 --repeats 7 --numbers
```

`make verify` runs c-utils' own tests and gate, checks the CLI entry point,
replays the framing oracle and its six output mutants, runs cgrep's deep
checks, and checks the actual executable with I/O budgets and 3000 differential
cases each for seeds 7 and 42. It rejects missing required checks or unexpected
skips. Authority, regions, absent CBMC proofs, and optional tcc remain outside
the required profile; Cerberus is explicitly bounded. On Linux it creates a
systemd user scope with a 3 GiB memory cap, no swap, reduced priority, and no
core dumps. There is no uncapped fallback.

Prepare the durable checker profile once with
`../dependable-c-agent-toolchain/bin/dcheck-tools build`. `make verify` then
selects its hash-validated Cerberus and Valgrind automatically; no temporary
PATH or opam overrides are needed. The profile lives under
`~/.local/opt/dcheck-tools/`, separately from system packages. Ordinary native
prerequisites such as zig, wasmtime, clang, and ripgrep must remain available
on PATH. See the sibling toolchain's `docs/checker-repairs/` for pinned build
prerequisites and regression probes. Missing or stale profiles fail rather
than falling back to the known-broken installed checkers.
Receipts are written under `build/verify.*`. This target checks correctness
and resource behavior, not benchmark parity.

[Gitea acceptance CI](ci/README.md) uses a dedicated trusted repository
runner and complete source pins. Check the latest Gitea job and acceptance
receipts before treating a published revision as CI-verified.

[Performance status](docs/PERFORMANCE.md) separates correctness acceptance
from quiet performance qualification. The strongest-competitor target remains
unmet. For executable identities, report limits and raw artifact locations,
see the [evidence index](docs/EVIDENCE.md). Historical measurements do not
qualify later source changes.

The literal benchmark compares cgrep, ripgrep, and `grep -F` with
`LC_ALL=C` for all tools, checking output and status before timing. Its JSON
records tool versions, samples, medians, and cgrep-to-reference time ratios.
It includes absent literals, a last-line match, repeated-prefix
failures, a misleading candidate byte, uniform patterns, and two hot
candidate bytes, including a three-hot-byte case. Pass
`--baseline /path/to/previous-cgrep` for a paired comparison. The benchmark
shuffles program order on every repetition. Fuzz harnesses cover byte
matching in `fuzz/fuzz_find.c` and exact emitted bytes, numbering, binary
warnings, and short-read/error framing in `fuzz/fuzz_stream.c`. The framing
oracle selects lines using naive search over the original input and observed
successful deliveries; it does not reuse the production finder or buffer.
Checked-in seeds exercise growth and multiple real 64 KiB refills. Per-harness
limits are 512 and 262144 bytes, respectively. Regenerate/check seeds with
`python3 -I tests/fuzz_seeds.py --write` / `--check`. `dcheck deep` runs them when the required tools exist.
Cerberus uses the explicitly bounded [semantics suite](semantics/README.md);
full native tests and both fuzz targets remain included. Text and JSON
results name the selected semantics suite. See [verification.md](verification.md)
for private-tool requirements and verification limits.

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

See [Runtime TODOs](TODO.md) for error contracts, resource limits, scoped
workers, bounded queues, cancellation, and concurrency verification.
