# cgrep

[![CI](https://github.com/Trav55555/cgrep/actions/workflows/ci.yml/badge.svg)](https://github.com/Trav55555/cgrep/actions/workflows/ci.yml)
[![cgrep vs rg](https://img.shields.io/endpoint?url=https://raw.githubusercontent.com/Trav55555/cgrep/bench-results/badge.json)](https://github.com/Trav55555/cgrep/tree/bench-results)

Streaming literal-text search in Dependable C. Search files, walk directory
trees, or filter standard input without loading whole files into memory.
The project exercises [c-utils](https://github.com/Trav55555/c-utils) with
real file I/O and search workloads.

**Experimental.** Patterns are literal, not regular expressions. This is
not yet a drop-in replacement for ripgrep.

[Quick start](#quick-start) · [Usage](#usage) ·
[Search behavior](#search-behavior) · [Limits](#limits) ·
[Development](#development) · [Roadmap and naming](#roadmap-and-naming)

## Quick start

You need Git, GNU Make, and a GCC-compatible C compiler, such as GCC or
Clang. The commands below assume a POSIX shell. Native builds and unit tests
do not need the Dependable C checker toolchain.

From a parent directory for your source checkouts, clone the repositories
side by side:

```sh
git clone https://github.com/Trav55555/c-utils.git
git clone https://github.com/Trav55555/cgrep.git
cd cgrep
make
```

Try a search without creating any files:

```sh
printf 'one\ntwo one\n' | ./build/cgrep -n one
```

Expected output:

```text
1:one
2:two one
```

Then search the source tree:

```sh
./build/cgrep -n return src/
```

The executable is `build/cgrep`. To use an existing c-utils checkout:

```sh
make CUTILS=/path/to/c-utils
```

## Usage

```text
cgrep [-0cilnvw] [-j N] [--hidden] [--no-ignore] [--max-depth N] PATTERN [PATH...]
cgrep --help | --version
```

**No path means stdin.** Pass `.` to search the current directory. A path
of `-` also reads stdin. Put options before the pattern.

### Common searches

Run these examples from the checkout root:

| Goal | Command |
| --- | --- |
| Search a directory with line numbers | `./build/cgrep -n return src/` |
| Match a word without ASCII case distinctions | `./build/cgrep -iw error README.md` |
| Print matching filenames | `./build/cgrep -l cu_arena src/` |
| Count selected lines per file | `./build/cgrep -c return src/` |
| Exclude lines that contain a literal | `./build/cgrep -v return src/cgrep.c` |
| Disable ignore-file filtering | `./build/cgrep --no-ignore return src/` |
| Use four workers with ordered output | `./build/cgrep -j4 -n return src/` |
| Search for a pattern that starts with `-` | `./build/cgrep -- -needle README.md` |
| Show the version and source revisions | `./build/cgrep --version` |

Quote patterns that contain spaces or shell metacharacters. For example,
`'a.*b'` searches for those four literal characters, not a regular expression.
Newline patterns are rejected.

### Options

| Option | Behavior |
| --- | --- |
| `-h`, `--help` | Print usage and option summaries to stdout, exit 0. |
| `-0`, `--null` | Follow printed paths with NUL, including the terminator in `-l` mode. Binary warnings retain their usual separator. |
| `--hidden` | Search dot files and directories while applying ignore rules. |
| `--max-depth N`, `--max-depth=N` | Root depth is 0; depth 1 searches immediate files. Explicit files are always searched. |
| `-n` | Add line numbers. |
| `-i` | Ignore ASCII letter case. Non-ASCII letters are not case-folded. |
| `-w` | Require no word character immediately before or after the match. |
| `-v` | Select lines that do not match. |
| `-c` | Print each input's count of selected lines. Omit zero counts. |
| `-l` | Print the name of each input with a selected line. |
| `-j N`, `-jN` | Use 1 through 32 workers. The default is one per online CPU, at most 32, where workers are available (Linux, glibc 2.34 or newer), else 1. |
| `--no-ignore` | Disable ignore files. Discovered hidden names remain skipped. |
| `--` | End option parsing. |
| `--version` | Print the version and cgrep and c-utils source revisions. |

Short flags can be combined, such as `-vn`, `-iwc`, or `-cj4`. If both `-c`
and `-l` appear, the later option wins. Neither mode prints line numbers.
Stdin is named `<stdin>` when filenames are requested.

### Exit status

| Status | Meaning |
| --- | --- |
| `0` | At least one line was selected. |
| `1` | No lines were selected. |
| `2` | A usage, input, close, allocation, resource-limit, or output error occurred. |

An input failure does not prevent later paths from being searched. An
observed output failure stops serial input or cancels prefetched workers.
On POSIX, a closed output pipe can terminate the process with SIGPIPE
instead of status 2.

## Search behavior

Directory searches visit names in byte order, depth first. Multiple input
paths, or any directory argument, produce filename prefixes. Parallel output
and diagnostics retain argument order.

Discovered symbolic links and non-regular files are skipped. An explicitly
supplied linked argument is followed. Explicit file arguments are never
filtered by hidden-name or ignore rules.

### Ignore files and hidden names

For directory searches, ignore sources have the following precedence,
highest first:

1. `.rgignore`
2. `.ignore`
3. `.gitignore`
4. `.git/info/exclude`
5. The global Git excludes file

The last three apply only inside a Git repository, identified by a `.git`
entry here or above. Within each source kind, deeper directories take
precedence. Ancestor ignore files above the search root also apply.

`!` rules can unignore paths, including hidden names. Otherwise, discovered
names beginning with `.` are skipped. Rules inside a pruned directory are
not loaded. Unreadable ignore files are silently skipped, as ripgrep does.

Each entry's verdict has a matching work limit, which every rule tried
extends, so large files of ordinary rules are matched in full. Adjacent
`**` components match as one, as in Git, so chains of them stay linear;
a pattern that still retries exponentially (several `**` separated by
other components against a deep path) exceeds the limit. That entry is
reported as `cgrep: PATH: ignore rules exceed the matching work limit`
and skipped, and the search exits with status 2.

`--no-ignore` disables these files, but does not enable hidden-name traversal.
Combine it with `--hidden` to include hidden names, or pass a hidden file explicitly.

The global file is `$XDG_CONFIG_HOME/git/ignore`, or
`~/.config/git/ignore` when that environment variable is unset or empty.
Git's `core.excludesFile` setting is not read. Backslashes are literal
inside bracket classes.

### Parallel searches

On Linux with glibc 2.34 or newer, regular files are searched by up to `-j N`
workers, by default one per online CPU (at most 32). Output order never
depends on the worker count: files appear in argument order, with each
directory's entries in byte order, depth first, exactly as at `-j1`.

Workers start only as unclaimed inputs pile up (sixteen per worker), so a
search of a few files or a small directory runs on the main thread and costs
the same as `-j1`. Directory arguments are listed on a separate thread while
workers search what is already listed. Each worker claims a batch of
consecutive inputs, from one input up to 64 and about 4 MiB of input per
claim, and hands its whole batch to the main thread in one step; the main
thread writes batches in claim order, which is input order, and is the only
writer of standard output and error. A walk failure is reported when the main
thread next writes, so it may appear before or after nearby search output.

Stdin stays serial. A path that turns out not to be a regular file is
searched serially when its turn comes. Unsupported targets and an unavailable
thread backend fall back to serial search; a worker that cannot start is
reported with status 2 and the search continues with the workers that did.
The pool joins every started thread before returning. Regular-file reads can
delay cancellation, which workers check every eighth refill. See the
[parallel design](docs/PARALLEL-DESIGN.md) for the measurements behind the
default and the [performance status](docs/PERFORMANCE.md).

## Limits

| Area | Current behavior |
| --- | --- |
| Patterns | Literal text only. Regex and terminal formatting are not implemented. |
| Text lines | At most 8 MiB before LF in printing modes. Exactly-at-limit lines work with LF or EOF. Longer lines report an error. |
| Count and filename modes | `-c` and `-l` keep no lines and treat NUL bytes as line ends. The 8 MiB line limit does not apply. |
| Binary input | Printing stops with a warning once a match and a NUL have been observed. Earlier printed lines remain visible. `-c` drops binary inputs. |
| Encoding | No BOM stripping or encoding conversion. CRLF is preserved. Matching final unterminated text lines receive a newline. |
| Case and words | `-i` does not fold `é` to `É`. `-w` treats all non-ASCII characters as word characters, including punctuation. |
| Ignore patterns | Malformed patterns lack ripgrep's diagnostics and can behave differently. A trailing space escaped by an odd run of backslashes is kept, as Git reads it; ripgrep 14 keeps it after any backslash. |
| Ignore matching work | A verdict that exceeds the matching work limit (1,000,000 units plus 1,024 per rule tried, or `**` retries nested more than 256 deep) is an error for that entry: reported, skipped, status 2. |
| Directory nesting | At most 256 levels below each root. Deeper directories report an error. An explicit maximum depth stops at min(N, 256) without error. |
| Input size | Delivered-byte counters use `unsigned long`. Input beyond `ULONG_MAX` bytes reports an error instead of wrapping. |
| Platforms | Native Windows and exotic integer-width ABIs remain unverified. |

Reads use blocking stdio and may read ahead. Immediate interactive output
is not promised. The input-window bound is per worker, not a total process
memory limit.

<details>
<summary>Streaming, binary detection, and memory details</summary>

Each worker starts with a reusable 64 KiB input window. It grows only for an
unfinished line. Geometric arena growth keeps window storage below 16 MiB,
plus metadata. Shared pattern storage, an O(pattern length) prefix table,
libc overhead, worker stacks, and output slots are additional costs.

Tests exercise serial 64 MiB files and pipes, and eight workers scanning
448 MiB under a 32 MiB address-space limit. These cases use a small pattern
and bounded-length lines. They do not establish that every workload fits
that process limit.

Each refill is checked for NUL before its text is searched. After NUL
appears, cgrep prints one binary warning if any match has been found,
including a match from an earlier refill, then stops that input. Otherwise
it searches subsequent chunks with persistent KMP state and no further
window growth. Previously printed lines stay printed. The warning uses
the first NUL's offset in delivered bytes.

For streaming comparisons, use `rg -F --no-config --no-mmap -j1` with piped
output. mmap-based ripgrep can differ on late NULs. Refill boundaries can
also differ for long lines.

Borrowed stdin starts at its current position, inherits its mode, and stays
open with read-updated flags. Partial output from earlier windows remains
visible after a later input failure.

</details>

## Development

Start with the native unit tests:

```sh
make test
```

Choose additional checks by the behavior you change:

| Command | Scope | Additional requirements |
| --- | --- | --- |
| `make test` | Strict C89 unit tests and native semantics harnesses | None beyond the native build requirements |
| `make difftest` | 400 generated comparisons against ripgrep | Python 3 and `rg` on PATH |
| `make iotest` | Native allocation, resource-limit, and output-failure checks | Python 3 |
| `make check` | Compiler matrix, sanitizers, wasm, coverage, CLI checks, and I/O budgets | Dependable C checker toolchain and its prerequisites |
| `make verify` | Complete resource-capped acceptance, including dependency checks | The provisioned checker profile and required native tools |
| `make mutate` | Isolated mutation runs for `src/cgrep.c` and `src/ignore.inc` | The installed c-verification mutation runner |

<details>
<summary>Set up and run full acceptance</summary>

The checker defaults to
`~/projects/dependable-c-agent-toolchain/bin/dcheck`. Override `DCHECK` in
make if needed. For a custom c-utils location, set `DEPS` in `dcheck.conf`
to the same dependency used by the build.

Prepare the durable checker profile once:

```sh
../dependable-c-agent-toolchain/bin/dcheck-tools build
```

This is an explicit provisioning step, separate from the native build.
See the toolchain's `docs/checker-repairs/` for pinned build prerequisites
and regression probes. The profile lives under `~/.local/opt/dcheck-tools/`,
separately from system packages. Native prerequisites such as zig, wasmtime,
clang, and ripgrep must remain available on PATH.

Then run:

```sh
make verify
```

On Linux, verification creates a systemd user scope with a 3 GiB memory cap,
no swap, reduced priority, and no core dumps. There is no uncapped fallback.
The command selects hash-validated Cerberus and Valgrind from the profile.
Missing or stale profiles fail instead of using known-broken installed
checkers. Temporary PATH or opam overrides are not needed.

Acceptance runs c-utils' tests and gate, CLI entry checks, framing and
serial controls, native worker controls, cgrep's deep checks, and executable
I/O budgets. It includes 12,000 ordinary differential cases across seeds
7 and 42 at `-j1` and `-j8`, plus 2,000 ignore-file comparisons. It also
checks that unsupported mutation selectors cannot reach the runner.

Missing required checks and unexpected skips fail acceptance. Authority,
regions, absent CBMC proofs, and optional tcc remain outside the required
profile. Cerberus uses the explicitly bounded
[semantics suite](semantics/README.md), not a full-program proof. Native
tests and both fuzz targets remain included. Text and JSON results name
the selected semantics suite.

Receipts are written under `build/verify.*`. This target checks correctness
and resource behavior, not benchmark parity. The
[Gitea acceptance CI guide](ci/README.md) describes the trusted runner,
complete source pins, and separately pinned ripgrep reference. Check the
job and receipts for a published revision before calling it CI-verified.
Local `make verify` does not inherit CI's reference-selection guarantee.

See [verification evidence and limits](verification.md) for executed checks,
private-tool requirements, and known gaps.

</details>

<details>
<summary>Mutation profiles and limits</summary>

`make mutate` uses the installed c-verification mutation runner. Each worker
rebuilds its own source copy. The default profile runs a walk-test preflight
with a 30-second runtime limit and 150 ignore-file comparisons, then unit
tests in native, combined ASan and UBSan, and MSan builds, followed by 150
ordinary differential cases.

For the ignore module alone:

```sh
MUTATION_FILE=src/ignore.inc MUTATION_TIMEOUT=90 make mutate
```

This profile runs only the ignore-unit and walk suites in each of the three
builds, with both 150-case comparisons retained. Full-suite integration
remains covered by `make verify`. The selector accepts only `src/cgrep.c`
or `src/ignore.inc`. Other paths fail before the runner starts.

Defaults are two workers, 30 seconds per command, a 16 MiB output-file
limit, a 768 MiB native address-space limit, and a 512 MiB ASan RSS limit.
Core dumps are disabled. Use an outer memory limit for aggregate worker
memory. Sanitizer builds need more virtual address space than the native
limit permits.

Override `JOBS`, `MUTATION_TIMEOUT`, or `MUTANTS` as needed. Leak detection
belongs to `make check`. Mutation ASan runs check bounds and undefined
behavior. The runner returns 1 if any mutation survives, including
equivalent mutations. Survivor arguments are in
[verification.md](verification.md).

</details>

### Benchmarks and test oracles

```sh
python3 -I tests/benchmark.py build/cgrep --repeats 7
python3 -I tests/benchmark_parallel.py build/cgrep --mib 64 --repeats 7
python3 -I tests/benchmark_parallel.py build/cgrep --mib 64 --repeats 7 --numbers
```

The literal benchmark compares cgrep, ripgrep, and `grep -F` under
`LC_ALL=C`. It checks output and status before timing, shuffles program
order on every repetition, and records versions, samples, medians, and time
ratios in JSON. Use `--baseline /path/to/previous-cgrep` for a paired
comparison.

[Performance status](docs/PERFORMANCE.md) separates correctness acceptance
from quiet performance qualification. The strongest-competitor target
remains unmet. Historical measurements do not qualify later source changes.
The [evidence index](docs/EVIDENCE.md) links executable identities, report
limits, and raw artifact locations.

<details>
<summary>What the generated tests cover</summary>

The literal benchmark includes absent literals, a last-line match,
repeated-prefix failures, a misleading candidate byte, uniform patterns,
two hot candidate bytes, and a three-hot-byte case.

Differential tests compare stdout bytes and status, and require both tools
to agree on whether stderr is empty. Cases include stdin, explicit `-`,
multiple and missing paths, empty patterns, literal metacharacters, binary
data, line boundaries, and larger text files around refill sizes. Detailed
error wording, BOM decoding, and long binary-line refill differences are
outside that generated comparison.

`fuzz/fuzz_find.c` covers byte matching. `fuzz/fuzz_stream.c` covers exact
emitted bytes, numbering, binary warnings, and short-read and error framing.
The framing oracle uses naive search over the original input and observed
successful deliveries. It does not reuse the production finder or buffer.
Its six output mutants test sensitivity.

Checked-in seeds exercise growth and multiple real 64 KiB refills. Harness
limits are 512 and 262144 bytes, respectively. Check or regenerate them with:

```sh
python3 -I tests/fuzz_seeds.py --check
python3 -I tests/fuzz_seeds.py --write
```

`dcheck deep` runs the fuzz targets when the required tools exist.

</details>

## Implementation notes

<details>
<summary>Literal matching, line counting, and ordered workers</summary>

The literal is prepared once per command. Two filter bytes come from a
static background byte-frequency rank derived from the memchr crate, not
from the pattern's own bytes. A candidate must match both bytes before
`memcmp` confirms it. Search starts with libc `memchr` on the rarer byte.
On x86-64, dense hits switch it to an SSE2 scan of both bytes at once.

Confirmation work is charged against bytes skipped. When it outruns them,
search switches once per input to a byte that actually mismatched. It then
uses Knuth-Morris-Pratt, or KMP, over one bounded window before resuming the
filter. Learned choices persist across matches within an input. Uniform
literals have a separate linear path. A mismatching terminal byte skips a
full window, and a failed comparison skips through its rightmost blocking
byte. Search work is O(input length + pattern length), including preparation.

Line counting runs only when `-n` requests it, up to each printed line.
On x86-64 it counts 16 bytes per step with SSE2 compare masks. Other targets
use `memchr`. After four nearby newlines, they count at most 1024 bytes with
exact word-lane masks. Byte loads use `memcpy`. Word counting is enabled
only for checked 32-bit or 64-bit unsigned-long representations with 8-bit
bytes. Four independent loads share a loop step, with up to 16 words per
lane reduction and no byte-lane carry. Other representations keep
character-only counting.

Without names or numbers, adjacent matching lines are written as one slice.
The Linux CLI uses a 64 KiB stdout buffer only for non-TTY output, and
glibc's unlocked stdio. Caller-owned library streams keep their existing
setup. Build with `-DCGREP_NO_SIMD` to force portable code.

Worker `w` searches paths `w`, `w + N`, and so on. Each started worker has
its own input window and two 1 MiB output slots, so it can search its next
file while the coordinator writes the previous one. Slots are allocated
only for started workers. c-utils supplies threads, mutexes, and broadcast
conditions. Worker stacks are 256 KiB. The pool joins the workers, destroys
the synchronization objects, then releases their arena-owned records.

See the [parallel design](docs/PARALLEL-DESIGN.md) for the ordering model.

</details>

## Roadmap and naming

Next steps:

1. A regex parser and Thompson NFA.
2. A lazy DFA, prefilters, and benchmarks against ripgrep.

One potential future name is **fyou**, short for "Found You". The repository
and executable remain `cgrep`.

See [runtime tasks](TODO.md) for error contracts, resource limits, scoped
workers, bounded queues, cancellation, and concurrency verification.

## Benchmark

![cgrep vs rg tree search benchmark](https://raw.githubusercontent.com/Trav55555/cgrep/bench-results/bench.svg)

The benchmark searches generated wide, many-directory, and mixed-size trees, including the directory walk.
Before timing, it validates cgrep output byte for byte against sorted ripgrep output and checks unordered ripgrep output as a multiset of lines.
It compares one thread and each tool's default threads using warm-cache median wall times, including process startup.
Shared runners are noisy: compare ratios across runs rather than absolute times.
Reports and history live on the [bench-results branch](https://github.com/Trav55555/cgrep/tree/bench-results).
