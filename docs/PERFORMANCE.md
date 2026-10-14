# Performance target and current baseline

The target is to meet or beat the strongest valid competitor on representative
literal-search workloads, while preserving cgrep's correctness, output-order,
error, and memory contracts. We are not there yet. The earlier seven-case
single-file comparison was too narrow to establish that target.

## Reproduce

```sh
python3 -I tests/test_parallel_benchmark.py
python3 -I tests/benchmark_parallel.py build/cgrep --mib 64 --repeats 7 --cgrep-threads --output plain.json
python3 -I tests/benchmark_parallel.py build/cgrep --mib 64 --repeats 7 --numbers --cgrep-threads --output numbered.json
```

Run sequentially inside the same 3 GiB/no-swap scope used for other heavy
checks, with reduced priority and core dumps disabled. GNU grep is required;
`--gnu-grep` can select its executable. `--locale` defaults to `C`. Repeat with
`--locale C.UTF-8` to check locale dependence.

Each tool gets the same explicit filenames and literal pattern. Ripgrep runs
with `-j1`, `-j2`, `-j4`, `-j8`, and default threads. `--cgrep-threads`
adds explicit cgrep `-j2/-j4/-j8`; its default stays serial. Directory traversal and
ignore filtering are not measured. Inputs are warm on tmpfs. All tools emit
complete output to real temporary files during timing, not `/dev/null`.
Captures before and after timing validate exact per-file bytes and line order;
only ripgrep may reorder files. Timed output lengths and statuses are checked.
The validator rejects lost, duplicated, and reordered lines or wrong statuses.

## Verified output and counting follow-up

The follow-up combines final channel bytes with job completion, releases the
pool mutex during output I/O, formats decimal prefixes without `sprintf`,
and emits an existing LF with its line body. The Linux CLI gives non-TTY
stdout a fixed 64 KiB buffer. TTY buffering and borrowed library streams keep
their existing setup. Dense counting reduces at most 16 copied words together;
the byte-lane total stays below 256. Sparse counting still uses `memchr`.

Seven randomized paired rounds against the published executable include
all fixed cgrep and ripgrep worker counts. The workstation was contended;
these wall medians are evidence for this run, not stable-host guarantees.
The gap-focused rows below use milliseconds. All cases, samples, aggregate
CPU times, versions, hashes, and further profiles are in
`performance-followup.json`.

| Workload | Before -j1 | Selected -j1 | Before -j8 | Selected -j8 | GNU grep | rg -j8 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Single absent, numbered | 23.82 | 22.16 | 23.04 | 22.95 | 25.48 | 12.68 |
| Medium absent, numbered | 22.23 | 20.65 | 6.97 | 7.62 | 24.81 | 7.32 |
| Small late match, plain | 13.02 | 12.68 | 24.66 | 17.92 | 10.46 | 10.59 |
| Small late match, numbered | 14.67 | 15.75 | 44.62 | 23.34 | 14.65 | 14.34 |
| Heavy output, plain | 31.72 | 27.41 | 28.37 | 25.15 | 28.93 | 18.65 |
| Heavy output, numbered | 51.80 | 37.21 | 54.59 | 36.76 | 43.85 | 26.58 |

Heavy numbered output improves about 28% serial and 33% at eight workers.
One exact-output trace reduces serial heavy-output writes from 3440 to 224.
The traced small-match run reduces futex calls from 20342 to 6779; this
schedule-dependent count is not a guarantee. Traced timings are not the
benchmark timings.

A separate 21-round numbered remeasurement gives small-match serial
15.20 to 14.50 ms and eight workers 39.91 to 24.74 ms. Medium absent serial
improves 24.87 to 22.85 ms, while eight-worker wall time is roughly unchanged,
13.82 to 14.09 ms. Its aggregate child CPU falls 26.10 to 24.55 ms. This
limits the apparent small serial regression and medium worker slowdown in
the first sweep; it does not establish universal nonregression.

The overall target remains unmet. Single-file numbering and small matching
files still trail strong competitors. Worker selection stays explicit.

### Broader explicit-file profiles

`benchmark_realistic.py` adds mixed line shapes, unequal sizes, and copied
real-repository text. Seven rounds cover plain/numbered output to regular
files and fully collected pipes, plus targeted cache advice on ext4. Every
timed invocation checks all output bytes and status. The repository manifest
records source names, hashes, exclusions, HEAD, and dirty state; reproducing
a dirty snapshot requires the matching working tree. Generated arguments
use ASCII filenames, so this does not measure original repository path costs
or any search tool's directory traversal.

```sh
python3 -I tests/test_realistic_benchmark.py
python3 -I tests/benchmark_realistic.py build/cgrep --baseline /path/to/control --work-root /path/on/chosen/filesystem --numbers --sink pipe --output mixed-pipe.json
python3 -I tests/benchmark_realistic.py build/cgrep --baseline /path/to/control --work-root /path/on/chosen/filesystem --case repository --repository ../c-utils --pattern cu_ --cache advise-cold --output repository-advised.json
```

The file-cache advice uses `fsync` and `POSIX_FADV_DONTNEED` only on generated
copies before each invocation, outside timing. It never drops global caches.
It is not proof of physically cold I/O. Pipe output is collected by the
parent, whose work contributes to wall time but not aggregate child CPU.
These new profiles do not establish terminal performance or universal parity.

The measured executable is the accepted pinned-dependency build,
SHA-256 `7ff504edb9f59d8381933a882c666f4df0f1c6a26afde9af4934d701569b0351`.
[Verification](../verification.md) records source identity and acceptance;
[Gitea CI](../ci/README.md) distinguishes local evidence from server-triggered
workflow results. Retain raw reports with
`tests/record_followup.py --evidence /path/to/reports --acceptance /path/to/ci-scratch --output docs/performance-followup.json`.

## Published opt-in parallel slice

Linux/glibc >= 2.34 regular-file searches now support 1 through 8 workers,
with argv-order output and fixed 64 KiB per-worker channels. Default remains
serial; stdin/nonregular paths and portable targets stay serial. Native
failure injection, required TSan/ASan checks, compiled behavioral mutants,
wasm, bounded semantics, memory limits, and 12,000 ordered differential
comparisons pass. Native pthread execution is not semantically interpreted.

The accepted executable's seven-repeat medians are milliseconds below.
These are fixed configurations, not a per-case minimum over thread counts.
All five ripgrep configurations and every raw sample remain in
`performance-parallel.json`.

| Workload | Serial | cgrep -j2 | cgrep -j4 | cgrep -j8 | GNU grep | rg -j8 | rg default |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| single-large-absent, plain | 10.42 | 10.11 | 10.17 | 9.79 | 11.20 | 8.83 | 8.53 |
| medium-absent, plain | 10.33 | 6.31 | 4.31 | 3.63 | 11.22 | 5.53 | 5.53 |
| medium-late-match, plain | 9.97 | 6.48 | 4.37 | 3.64 | 10.73 | 5.40 | 5.60 |
| medium-uniform-stress, plain | 10.14 | 6.41 | 4.27 | 3.39 | 14.12 | 30.25 | 27.78 |
| small-absent, plain | 7.78 | 11.50 | 8.16 | 7.36 | 7.91 | 6.36 | 7.31 |
| small-late-match, plain | 8.50 | 17.88 | 13.93 | 12.87 | 8.32 | 6.67 | 7.01 |
| matching-output-heavy, plain | 23.97 | 18.63 | 19.75 | 19.96 | 19.88 | 10.11 | 10.68 |
| single-large-absent, numbered | 18.16 | 18.67 | 18.63 | 17.75 | 20.76 | 8.86 | 8.71 |
| medium-absent, numbered | 18.01 | 11.00 | 6.53 | 4.93 | 21.53 | 5.60 | 5.70 |
| medium-late-match, numbered | 18.29 | 11.26 | 6.61 | 4.75 | 21.63 | 5.73 | 5.87 |
| medium-uniform-stress, numbered | 18.43 | 10.43 | 6.44 | 4.54 | 22.09 | 27.91 | 27.93 |
| small-absent, numbered | 9.63 | 12.34 | 8.56 | 8.07 | 10.35 | 6.82 | 7.23 |
| small-late-match, numbered | 11.10 | 19.00 | 14.03 | 13.34 | 10.87 | 7.12 | 7.68 |
| matching-output-heavy, numbered | 36.08 | 31.15 | 32.16 | 33.52 | 30.82 | 12.55 | 12.70 |

Medium-file ordinary scans at `-j8` beat every measured ripgrep thread
configuration and GNU grep in these runs. Plain medium scans are about
2.7–2.8x faster than same-build serial; numbered scans are about 3.7–3.9x
faster. Uniform stress also beats both competitors.

The target remains unmet overall. Single-file numbering is roughly twice
ripgrep's time. Small late-match files regress with workers, and output-heavy
search still trails parallel ripgrep by about 2x plain and 2.7x numbered at
`-j8`. Two workers improve heavy plain output to 18.63 ms versus serial
23.97 ms and GNU's 19.88 ms, but ripgrep still takes 10.11 ms. These distinct
tradeoffs do not justify automatic selection.

The initial stdio-cookie bridge regressed heavy output and exposed an MSan
instrumentation boundary in libc's internal buffer. A direct writer callback
removed that boundary without suppression and improved the measured output
path. Serial formatting remains unchanged.

Acceptance: `build/verify.e3SwYZ`. The measured and accepted executable has
SHA-256 `1d247834a63137b881013b8a2de7b27f39d7b8b204531c66a5c33b0e7420bd8e`.
Scope and raw samples are preserved in `performance-parallel.json`; local
logs are in `/tmp/cgrep-parallel-scope.laLQae`. Cold I/O, traversal, mixed
repository files, uneven-file benchmarks, and terminal/pipe performance
remain unmeasured. Single-file `-j` variations are noise, not parallel speedup.

## Historical verified serial slice

Uniform literals now skip full windows on terminal blockers and skip through
the rightmost mismatch after a failed full comparison. Discovery scans are
capped at the pattern length. Two failed full comparisons advance at least
one pattern length, preserving linear search work. Cross-chunk binary search
still uses the existing persistent KMP state.

Numbering uses exact copied-word byte masks only after four nearby newlines,
for a bounded 256-byte batch. It then returns to `memchr`, keeping sparse
inputs fast. Always-on word counting was rejected because it nearly doubled
sparse-input time; plain unrolling was slower than the baseline. The selected
word path checks both unsigned-long value width and object size, uses `memcpy`
for unaligned loads, and avoids borrow-based zero masks that overcount adjacent
zero/one bytes. Other representations keep the character-only path.

Final paired medians, same input and full-output sink, seven repetitions:

| Workload | Before | After | GNU grep | rg -j8 |
| --- | ---: | ---: | ---: | ---: |
| Uniform stress, plain | 89.43 | 9.50 | 13.25 | 29.10 |
| Uniform stress, numbered | 102.39 | 17.44 | 21.60 | 30.68 |
| Medium absent, numbered | 20.34 | 17.74 | 20.94 | 5.37 |
| Medium late match, numbered | 19.96 | 17.69 | 21.17 | 5.47 |
| Matching-heavy output, numbered | 35.75 | 36.53 | 31.21 | 13.08 |

The uniform plain case improves about 9.4x and is about 1.4x faster than GNU
grep in this run. Ordinary numbered scans improve about 11–13%. Output-heavy
performance remains roughly unchanged, with no claimed gain. A separate
21-repeat check of ordinary medium absent and small late-match cases finds
before/after medians essentially equal; an earlier small apparent slowdown
was within run-to-run variation.

`make verify` passes on the selected source, with 94.08% cgrep line coverage,
all 6000 differential comparisons, the native budgets, wasm, sanitizers,
Valgrind, and bounded semantics. New exact-size count tests cover alignment,
tails, density changes, all 256 byte values, and adjacent LF/VT lanes. Uniform
search tests cover lengths, blockers, starts, high/NUL bytes, final matches,
and bounded comparison work. Compiled overstep, borrow-mask, and lane-sum
defects are rejected natively and by Cerberus.

Receipts and rejected scratch candidates are in
`/tmp/cgrep-serial-prototypes.iigr0Q`; acceptance is also recorded under
`build/verify.okbYkH`. The final measured executable SHA-256 is
`d373ff111410fb77380d981de8b80b20c541efaec57449408f7c02e764eec04b`.
This historical serial slice preceded the parallel backend recorded above.
Its strongest-competitor target was met for uniform stress only, not overall.

## Frozen pre-optimization baseline

Seven randomized paired repetitions on Linux x86_64, 12 logical/affinity CPUs,
ripgrep 15.2.0 and GNU grep 3.11. Automatic mmap remains enabled for ripgrep.
The baseline search code is from `7ae4400`; its measured executable
SHA-256 is `39f58a3a433e616c5931dba5a1cb2142fbfc383bdb580a72594ee8c218636b87`.
Medians below are milliseconds in the `C` locale.

### Without line numbers

| Corpus | Files/input | cgrep | GNU grep | rg -j1 | rg -j8 | rg default |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Single large, absent | 1 / 64 MiB | 9.42 | 10.47 | 7.89 | 7.53 | 7.35 |
| Medium, absent | 16 / 64 MiB | 9.46 | 10.62 | 10.16 | 5.13 | 5.26 |
| Medium, late match | 16 / 64 MiB | 9.40 | 10.44 | 10.66 | 4.94 | 5.17 |
| Uniform repeated-byte stress | 16 / 64 MiB | 91.20 | 14.66 | 109.90 | 30.02 | 26.99 |
| Small, absent | 1024 / 16 MiB | 6.75 | 7.09 | 11.29 | 6.21 | 7.26 |
| Small, late match | 1024 / 16 MiB | 7.90 | 7.86 | 12.72 | 6.32 | 7.60 |
| Matching-output-heavy | 16 / 8 MiB | 22.90 | 19.35 | 29.84 | 9.62 | 10.05 |

### With line numbers

| Corpus | cgrep | GNU grep | rg -j1 | rg -j8 | rg default |
| --- | ---: | ---: | ---: | ---: | ---: |
| Single large, absent | 19.32 | 20.31 | 7.69 | 7.38 | 7.47 |
| Medium, absent | 19.27 | 20.09 | 11.10 | 5.24 | 5.30 |
| Medium, late match | 19.33 | 20.39 | 11.59 | 5.12 | 5.48 |
| Uniform repeated-byte stress | 99.78 | 21.03 | 112.54 | 29.25 | 25.53 |
| Small, absent | 9.74 | 10.22 | 11.15 | 6.50 | 6.92 |
| Small, late match | 10.79 | 10.46 | 13.25 | 6.61 | 7.79 |
| Matching-output-heavy | 34.92 | 29.80 | 41.99 | 11.90 | 12.02 |

The `C.UTF-8` controls preserve the broad ranking. GNU grep's unnumbered
uniform-stress median is 15.33 ms versus cgrep's 90.19 ms. Ripgrep's best
medium-file results remain around 5 ms. These controls do not establish
parity on non-ASCII patterns or other locales; all corpora here are ASCII.

## What this says to optimize

1. **Repeated-byte/scalar fallback.** At baseline, GNU grep was about 6.2x faster in the
   unnumbered stress case. Beating rg -j1 there was not sufficient evidence.
   Test alternative skipping/search policies against this frozen input and
   varied nearby distributions, with naive oracles and fallback sensitivity.
2. **Line counting.** `-n` roughly doubles cgrep's ordinary absent-search
   time, despite empty output. Improve counting work before attributing the
   entire numbered gap to missing threads. Preserve skipped-line counts,
   final tails, CRLF, and byte-count overflow behavior.
3. **Multi-file parallelism.** Ripgrep gains roughly 2x on medium ordinary
   files and 4x on the CPU-heavy stress case. A bounded worker design must
   retain input-file ordering, one output writer, failure continuation, and
   predictable memory/cleanup. Do not infer linear speedup from thread count.
4. **Output path.** GNU grep beats cgrep on serial matching-heavy output,
   and parallel ripgrep is about 2.4x faster without numbering and 2.9x with
   it. Profile formatting/writing separately from matching.

Cgrep's observed peak RSS is about 1.4–1.8 MiB. GNU grep is about 1.9–2.1 MiB;
parallel ripgrep uses roughly 4–18 MiB here, and its single-file mmap control
shows about 68 MiB resident. These are diagnostic observations, not heap-only
measurements or guarantees. Keep the existing memory contract while reducing
latency; a faster result obtained by dropping output or error checks is invalid.

## Limits and receipts

Raw samples, aggregate CPU time, diagnostic peak RSS, corpus fingerprints,
and both locale/numbering profiles are in `/tmp/cgrep-parallel-bench.sS7iyV`.
The final reports are `with-gnu-plain.json`, `with-gnu-numbered.json`, and the
matching `with-gnu-utf8-*.json` files. An earlier ripgrep-only numbered run used
a discarded-output sink and is retained only as exploratory history.

Single-file variations across `-j` settings are small/noisy and are not evidence
of parallelizing one file. Selecting the minimum across thread settings can
also favor noise; the tables retain fixed `-j1`, `-j8`, and default settings.
Cold physical-disk I/O, directory walking, mixed real-repository contents,
unequal file sizes, output to terminals/pipes, and changing distributions
remain unmeasured. These samples define the next optimization workload, not
universal superiority or a performance acceptance threshold for every input.
