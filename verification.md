# Literal-search verification

Streaming input, the selective matcher, and optional line counting pass
the native and wasm gates. Boundary, memory-budget, error, and fuzz checks
support the implementation. The project remains partially verified: deep
checks have tool failures. Mismatch-guided selection closes the measured
two- and three-hot-byte gaps. The tested corpus is not evidence of universal
performance parity or a formal correctness proof.

## Reproduce

Keep c-utils and dependable-c-agent-toolchain beside this checkout. Run
heavy checks sequentially under an outer memory cap when the devbox is
unavailable.

```sh
make test difftest iotest
make check
python3 -I tests/difftest.py build/cgrep --cases 3000 --seed 7
python3 -I tests/difftest.py build/cgrep --cases 3000 --seed 42
python3 -I tests/benchmark.py build/cgrep --mib 64 --repeats 7
make mutate
```

For paired timings, add `--baseline /path/to/previous-cgrep`. The benchmark
checks output and status before timing and shuffles program order each
round. `make mutate` returns nonzero for surviving mutations, including
functional equivalents and tuning changes. Its regular-file limit does
not bound subprocess pipe captures, so the outer memory cap matters.

## Current mismatch-guided evidence

The matcher inspects the first failed comparison once, relative to that
candidate's start. A different expected byte at the mismatch becomes the
suggested alternate; otherwise the pattern-derived alternate remains.
The choice is local to the finder call, not stored in the shared Literal.
Only one switch is permitted, with at most sixteen false confirmations
before KMP. The extra diagnostic pass adds O(m) work once, so the O(n + m)
search bound and buffer ownership are unchanged.

Raw logs, the published control binary, paired benchmarks, and rerunnable
`sensitivity.py` are in `/tmp/cgrep-mismatch-guided.s6I0CZ`. Runs used a 3 GiB
memory cap, no additional swap, and reduced CPU priority.

- The compiler, analyzer, ASan/UBSan, MSan, release, and wasm gate passed.
  Coverage was 93.09%. Existing authority/regions skips and the documented
  deep-tool blockers remain unchanged.
- 400 default differential cases and 3000 cases each with seeds 7 and 42
  passed against the supported streaming reference. Unsigned-char tests
  and native memory-budget/I/O checks passed.
- Finder fuzzing completed 7,402,396 executions in 31 seconds, and framing
  fuzzing completed 133,592 executions in 31 seconds without oracle or
  sanitizer errors. Existing size caps were retained.
- Work-counter regressions failed on the published matcher: hot pattern
  bytes should not force a full KMP scan when the observed mismatched byte
  is absent. Fixtures include two/three hot values, nonzero search starts,
  a final positive match, and a mismatch against the active filter's value.
- The previous overlap fixture remains a correctness test. A new repeated-
  byte fixture deliberately forces both rejection budgets and an overlapping
  KMP resume with a nonzero suggested offset.
- Six defects compiled and failed assertions: ignoring the observed mismatch,
  diagnosing relative to the wrong origin, skipping starts during switching,
  skipping an overlapping fallback match, disabling bounded fallback, and
  subtracting the original anchor after switching. This is targeted
  sensitivity, not a new full mutation score.

Seven paired warm-cache repetitions on tmpfs, stdout discarded, randomized
program order. Median milliseconds for 64 MiB input:

| Corpus | Published control | Mismatch-guided | ripgrep auto-mmap |
| --- | ---: | ---: | ---: |
| Absent literal | 7.56 | 7.51 | 7.13 |
| Last-line match | 8.32 | 8.25 | 7.45 |
| Repeated-prefix failures | 8.91 | 8.77 | 7.48 |
| Original misleading byte | 8.55 | 8.53 | 8.06 |
| Uniform pattern | 90.05 | 89.02 | 107.23 |
| Two hot values | 81.81 | 8.68 | 28.70 |
| Three hot values | 71.16 | 9.47 | 21.39 |

The two/three-hot cases improved about 9.4x and 7.5x. Ordinary cases were
roughly unchanged. All seven measured cases fall within 2x ripgrep in this
run, but matched-output-heavy workloads, many small files, cold I/O, and
changing byte distributions have not been evaluated here. A bad suggested
filter can still enter KMP; the heuristic does not promise SIMD throughput
on every input.

## Previous adaptive-filter evidence

The matcher now prepares a distinct alternate candidate byte. Each filter
gets a constant rejection budget before switching once or falling back to
KMP. The pattern stays immutable across inputs. Candidate starts advance
by one after a failed confirmation; switching changes the offset used by
`memchr`, not the set of already rejected starts. At most sixteen full
confirmations precede the linear fallback, preserving O(n + m) search work.

Raw logs, the prior streaming binary, paired data, prototype, and rerunnable
`sensitivity.py` are in `/tmp/cgrep-anchor-switch.V9pXl3`. All work was local
under a 3 GiB memory cap, no additional swap, and reduced CPU priority.

- Native/wasm gate passed, including compiler matrix, analyzer, ASan/UBSan,
  MSan, release, and memory-budget I/O tests. Source coverage was 92.87%.
  Authority and regions remained explicit skips.
- 400 default comparisons and 3000 cases each with seeds 7 and 42 matched
  ripgrep 15.2.0's supported streaming behavior.
- Finder fuzzing completed 7,988,347 executions in 31 seconds with inputs
  capped at 512 bytes and no sanitizer/oracle failures.
- A byte-step sensor failed on the old matcher: the misleading-byte fixture
  should reject its suffix using the alternate filter without a KMP scan.
- Exact-size and overlapping fixtures cover candidate-base subtraction,
  transition between filters, and resumption into KMP after both budgets.
- Five isolated defects compiled and failed assertions: skipping the
  alternate, skipping starts during the switch, subtracting the wrong
  anchor, disabling bounded fallback, and skipping an overlapping match
  when entering KMP. These are scoped sensitivity checks, not a new full
  mutation score.

Seven paired warm-cache repetitions, randomized program order, stdout
discarded. Median milliseconds for 64 MiB input:

| Corpus | Before switching | Adaptive filters | ripgrep auto-mmap |
| --- | ---: | ---: | ---: |
| Absent literal | 8.10 | 7.95 | 7.79 |
| Last-line match | 8.46 | 8.12 | 8.17 |
| Repeated-prefix failures | 9.65 | 9.56 | 8.06 |
| Original misleading byte | 125.50 | 9.09 | 8.73 |
| Uniform pattern | 93.33 | 93.00 | 114.19 |
| Two hot candidate bytes | 84.41 | 84.26 | 29.50 |

The original bottleneck improved about 13.8x. Uniform patterns have no
useful alternate and retain the existing linear fallback. When both
chosen bytes occur frequently, the scalar fallback remains slower than
ripgrep. The new fixture records that limit rather than declaring general
performance parity. At 8 MiB it measured 11.55 ms versus ripgrep's 5.55 ms.

The historical streaming and matcher results below apply to their earlier
source snapshots, not the current adaptive implementation. No toolchain
installation, commits, or pushes occurred in this step.

## Previous streaming evidence

Raw logs, the prior whole-file binary, paired benchmarks, minimal tool
reproductions, and the rerunnable `sensitivity.py` script are in
`/tmp/cgrep-streaming.tgHPjo`. Runs used a 3 GiB memory cap, no additional
swap, and reduced CPU priority. The reference is ripgrep 15.2.0. Differential
comparisons use `--no-mmap`; performance comparisons retain automatic mmap.

| Claim | Evidence and observed result |
| --- | --- |
| Chunked platform input preserves its protocol | `tests/test_input.c` covers owned and borrowed streams, exact and partial reads, terminal caching, NULL/closed misuse, read-error precedence, errno zero, close failures, and first-error preservation |
| Refills preserve text and numbering | `tests/test_stream.c` covers short reads, matches around 64 KiB boundaries, CRLF and UTF-8 splits, no-match windows before numbered matches, and final unterminated tails |
| Binary handling retains the first offset and cross-chunk matches | Unit cases cover initial/late NUL, earlier printed lines, the three-byte initial probe, partial KMP state, multiple NULs, empty/one-byte patterns, and NUL at the line-cap probe |
| Long lines respect the exact limit | 8 MiB minus one and exactly 8 MiB work at EOF; exactly 8 MiB followed by LF works; one byte over reports the line limit; a following numbered line has the right number |
| Memory no longer scales with total file size | Native checks search 64 MiB text files and pipes under a 32 MiB address-space limit; a measured absent-literal run used 1564 KiB maximum RSS versus 67004 KiB for the prior whole-file binary |
| Failures preserve cleanup and continued search | Initial/growth allocation failures reset the failed buffer before later paths; read and close failures keep their first cause; earlier printed output remains after a later read failure |
| Output failure stops later inputs | A read-only output descriptor causes status 2 without opening a later blocking FIFO; `/dev/full` also returns status 2 |
| Behavior matches the supported streaming reference | 400 generated cases plus 3000 cases with each of seeds 7 and 42 passed, including larger text inputs around refill sizes |
| Supported compile/runtime targets remain green | Both project gates passed: compiler matrix, analyzer, ASan/UBSan, MSan, release, and wasm; source coverage was 91.76% in c-utils and 92.59% in cgrep |
| Byte matching and framing have independent sensors | Finder fuzzing completed 9,020,570 executions in 31 seconds; the memory-reader framing harness completed 53,262 executions in 31 seconds, including seeded long text and binary inputs |

Authority and regions were explicitly skipped. Seven isolated mutations
compiled and failed assertions: publishing bytes on read error, losing an
earlier errno zero, closing borrowed streams, dropping EOF tails, losing
numbering across windows, resetting binary prefix state, and accepting
byte-count overflow. The framing fuzz harness compares match existence
with a naive byte oracle and checks injected I/O errors. It does not replace
the unit output and line-number expectations.

### Streaming performance

Seven paired warm-cache repetitions on tmpfs, stdout discarded, randomized
program order. Times are median milliseconds for 64 MiB input:

| Corpus | Whole-file before | Streaming | ripgrep auto-mmap |
| --- | ---: | ---: | ---: |
| Absent literal | 32.58 | 7.83 | 7.92 |
| Last-line match | 32.95 | 9.34 | 8.36 |
| Repeated-prefix failures | 33.01 | 8.88 | 8.46 |
| Misleading candidate byte | 136.15 | 122.98 | 8.55 |
| Uniform pattern | 114.40 | 94.17 | 112.84 |

In this pre-adaptation snapshot, the first three cases were close to
ripgrep while the misleading-byte case was about 14.4x slower. That
measurement motivated the adaptive-filter step above. At 8 MiB, absent/last-line/repetitive searches measured about 2 ms.
These are local end-to-end timings, not a general ranking of search tools.

### Current limits and blocked checks

The line limit excludes LF and includes CR. Geometric growth retains under
16 MiB of window storage plus allocator metadata, with pattern and libc
storage additional. The 32 MiB process-budget tests use a small pattern;
there is no configurable total-memory budget. Counters reject input beyond
`ULONG_MAX` delivered bytes. Blocking stdio and buffered output do not
promise immediate interactive delivery or cancellation.

NUL checks occur before text search in each refill. Previously printed
lines remain visible after a later binary warning. Initial refill size is
three bytes to match the reference's encoding probe, but cgrep does not
strip BOMs or transcode. mmap binary policy, long binary-line refill
boundaries, Windows stream modes, and exotic ABIs remain outside the
compatibility evidence.

Valgrind 3.27.1 passes the new input-library test but crashes in its own
replacement `memset` during the framing test. `valgrind_probe.c` reproduces
this independently of cgrep: native 65536-byte fill passes, Valgrind's
8-byte fill passes, and its 65536-byte fill exits with SIGILL. Disassembly
of `vgpreload_memcheck-amd64-linux.so` shows EVEX `vpbroadcastq` at offset
`0x135df`, the reported illegal instruction. The crash's leak summary is
not a completed leak check. The installed checker was not patched and the
tests were not reduced to hide it.

Cerberus remains blocked by its absent virtual `/tmp` and incorrect mixed
buffered `fwrite`/`fprintf` ordering. A requested independent review could
not run because the delegated provider rejected its token/credit budget.
The receipts above are executed checks and main-thread review, not an
independent review or formal proof.

## Previous matcher-stage evidence

The results below describe the pre-streaming source, not current timings.
Raw logs, control source/binary, paired benchmark data, and a rerunnable
sensitivity script are in `/tmp/cgrep-literal-opt.M8p21h`. This is local,
temporary evidence. Runs used a 3 GiB cgroup memory cap, no additional
swap, and reduced CPU priority. The matcher source SHA-256 is
`ed1586b8abbc4756806394756ec226776a274affd3d4941c086368baaecc1a61`.

| Claim | Evidence and observed result |
| --- | --- |
| Literal output and status stay unchanged | Unit cases and 400 default differential cases passed; 3000/3000 cases passed with each of seeds 7 and 42 against ripgrep 15.2.0, `-F --no-config -j1` |
| Candidate offsets handle byte and buffer boundaries | `test_find.c` exhaustively checks short strings over NUL, `a`, byte 233, and byte 255 against a naive oracle, including all starts through one beyond the end; an exact-size heap fixture runs under ASan |
| Prefix fallback preserves overlapping and final matches | Both finder paths run against the naive oracle; targeted cases force fallback, overlap across its resume point, and a final legal match |
| False confirmations stay bounded | Adversarial long-prefix cases use a comparison counter, not wall-clock assertions; selective-byte fixtures also require zero full confirmations when that byte is absent |
| Line counting is optional | A newline-search counter observes no counting scans without numbering and nonzero counting scans with it; CLI tests independently verify output |
| Pattern allocation failure is reported and cleaned up | Injected allocation refusal checks literal initialization failure and CLI status 2; gate leak checks and targeted valgrind pass |
| C89 and exercised targets remain supported | Fast check and compiler matrix passed; analyzer, ASan/UBSan, MSan, optimized release, and wasm passed; source coverage was 98.17%; `-funsigned-char` tests also passed |
| Arbitrary-byte search stays within bounds and matches the oracle | ASan/UBSan libFuzzer run completed 10,273,273 executions in 31 seconds without errors, with inputs capped at 512 bytes |

Authority and regions were explicitly skipped because they are not
configured. Native I/O checks for allocation refusal followed by a
readable input, and `/dev/full`, passed. Targeted valgrind checks covered
the CLI tests and the strengthened finder tests with leak checking.

The fuzz run used:

```sh
clang -std=c99 -g -O1 -fsanitize=fuzzer,address,undefined \
  -fno-sanitize-recover=all -Iinclude -I../c-utils/include \
  -o /tmp/cgrep-literal-opt.M8p21h/fuzz_find \
  fuzz/fuzz_find.c src/*.c ../c-utils/src/*.c
/tmp/cgrep-literal-opt.M8p21h/fuzz_find \
  -max_len=512 -max_total_time=30 -rss_limit_mb=512 \
  -artifact_prefix=/tmp/cgrep-literal-opt.M8p21h/
```

The harness exercises the production finder code, not the CLI's file I/O.
It is a C99 driver around C89 sources. The regular gate separately checks
the C89 subset.

## Linear-work argument

Knuth-Morris-Pratt search, or KMP, uses a prefix table. Pattern preparation
builds it in O(m) time and space, where m is the pattern length. The
pattern-local byte histogram is a heuristic, not a claim about the input's
byte frequencies.

Each finder call scans candidates monotonically, permits only a constant
number of full confirmations, then falls back to KMP. The prefix table
makes that fallback linear in the remaining input.

The CLI rejects newline-containing patterns. A successful nonempty-pattern
call therefore consumes a distinct line containing at least m bytes. Its O(m)
confirmation cost can therefore be charged to that line. There is at most
one terminal unsuccessful call per complete-line window. Its O(m) cost
occurs only when at least m bytes are available, so it can be charged to
that window. Empty patterns simply advance through the lines.

Streaming checks NUL and the last LF only in new bytes. An unfinished tail
is not repeatedly searched or moved. After a complete prefix is consumed,
only a suffix of the new read remains to compact. Geometric growth copies
are amortized linear, and binary KMP retains state across refills.

Thus total search work is O(n + m), including preparation, rather than
O(n * m). The byte-search oracle, comparison
sensor, and fuzzing test plausible mistakes in this argument; they are not
a formal proof.

## Sensitivity and mutation triage

The scoped sensitivity script planted four defects in isolated copies.
Each compiled and then failed an assertion:

- Removing the bounded-confirmation fallback.
- Resuming after the anchor instead of after the rejected candidate.
- Discarding a valid overlapping prefix in KMP.
- Counting lines when numbering is absent.

The first full mutation campaign exposed verification gaps: byte 255 was
missing from the oracle alphabet, deeper prefix borders needed longer
patterns, and mutation commands lacked MSan for uninitialized-table
mutations. The strengthened checks cover those gaps in the final campaign.

The final command classified 176 mutations as 148 killed, 14 timed out,
10 surviving, and 4 invalid. Timeouts are bounded failures, not proof of
which defect occurred. All ten survivors preserve the tested functional
results and linear-work bound; some alter costs or heuristic choices:

| Surviving change | Reason |
| --- | --- |
| `sizeof frequency[0]` to `sizeof frequency[1]` | Same element type; neither expression reads the element |
| Initialize histogram counts to 1 rather than 0 | Uniform shift preserves ranking; successful prefix allocation bounds the pattern length below count overflow on exercised ABIs |
| Start anchor selection at offset 0 rather than 1 | Redundant consideration of the already selected first byte |
| Use `<` instead of `<=` for ranking | Selects a different equally frequent byte on ties |
| Bypass the single-byte fast branch | Generic confirmation at the matching byte returns the same result; no fallback-table access is needed |
| Start rejection counter at 1 | Enters the linear fallback earlier |
| Use `!= 8` instead of `== 8` | Enters the linear fallback on the first rejected candidate |
| Threshold 8 to 9 | Another constant confirmation budget, with the same results and asymptotic bound |
| Newline-count bound `<` to `<=` | Extra zero-byte search returns NULL without reading data |
| Initial errno 0 to 1 | Successful reads do not use it; tested native I/O failures set their own errno, and allocation failure uses the arena flag |

This is not a 100% mutation claim, nor a claim that all heuristic variants
have identical performance.

## Historical whole-file matcher performance

Seven warm-cache repetitions, one explicit file, stdout discarded, and
randomized program order produced these median milliseconds. These are
end-to-end process measurements on tmpfs, not isolated matcher timings.

| Corpus | 8 MiB before | 8 MiB after | ripgrep | 64 MiB before | 64 MiB after | ripgrep |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Absent literal | 4.56 | 5.01 | 2.55 | 31.47 | 29.94 | 7.92 |
| Last-line match | 5.96 | 4.81 | 2.37 | 41.98 | 32.80 | 7.85 |
| Repeated-prefix failures | 37.27 | 5.38 | 2.71 | 282.64 | 31.91 | 7.83 |
| Misleading candidate byte | 37.43 | 17.19 | 2.50 | 283.01 | 128.26 | 7.83 |
| Uniform pattern | 36.54 | 15.61 | 16.24 | 277.52 | 111.92 | 110.80 |

The repeated-prefix case improved 6.9x at 8 MiB and 8.9x at 64 MiB.
Ordinary absent-literal timings are roughly unchanged; these runs do not
establish a general improvement there. Whole-file allocation and copying
remain. The misleading-byte case demonstrates that linear fallback alone
does not achieve ripgrep's selective SIMD throughput.

## Remaining feature limits

Recursive traversal, regex, ignore rules, terminal formatting, and parallel
search are absent. Detailed error wording differs from ripgrep. POSIX SIGPIPE can terminate the CLI
rather than returning output-error status 2. Native Windows and exotic
integer-width ABIs remain unverified.

Deep verification remains blocked by Cerberus's missing virtual `/tmp`
and its incorrect mixed buffered `fwrite`/`fprintf` ordering. Minimal
reproductions are in `/tmp/cgrep-deep.zE6slH`; no tests or gates were
weakened to hide those failures. Earlier file-reader and toolchain evidence
remains in `/tmp/cgrep-resume-checks.bW2Hhj`.
