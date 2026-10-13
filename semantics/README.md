# Bounded C semantics suite

`dcheck.conf` selects this directory through `SEMANTICS_TEST_DIR`. Cerberus
runs these four harnesses, not the large finder and streaming corpora in
`tests/`. dcheck reports that scope in text and JSON. Native checks still
run all original tests plus these harnesses. `make test` also runs both groups.
The framing fuzzer additionally checks exact output and seeded large refills;
see `../verification.md` for that separate evidence.

The harnesses include existing test seams and oracles. They neither change
production search logic nor override the 64 KiB chunk or 8 MiB line limit.
Their renamed native-suite entry points are not called by the bounded
finder and stream entry points.

| Harness | Bound | Checked behavior |
| --- | --- | --- |
| `sem_cli.c` | Existing CLI suite, unchanged | Arguments, numbering, file/stdin selection, binary output, errors and status |
| `sem_find.c` | Two-byte haystacks and needles over two byte values; targeted haystacks/counting spans at most 300 bytes | Naive-search oracle at every start, all two-chunk splits, empty and high-byte patterns, NUL bytes, exact-size allocation, overlap, both rejection budgets and KMP fallback, immutable filters, uniform-window blockers, dense word counting, preparation failure |
| `sem_stream.c` | Payloads at most 24 bytes; short reads of 1 through 7 bytes | Line/tail framing, numbering, binary matches split across reads, first NUL offset, partial output and scanner result on read failure, owned/borrowed stream cleanup, close failure, allocation recovery and later-file continuation, byte-counter overflow |

`sem_parallel.c` checks worker-count parsing, serial fallback options,
13-byte callback framing/numbering, and failures at each writer call with
a 192-byte capture. It does not interpret the native pthread backend.
Native worker scheduling, error injection, and race controls remain in
`tests/test_parallel.c` and `tests/parallel_check.py`.

Each entry point prints its `sem_NAME: PASS` marker only after assertions
finish. Cerberus may exit zero on failures, so inspect diagnostics as well
as markers and process status.

## Sensitivity

Run the checks with an explicit repaired Cerberus launcher:

```sh
python3 -I semantics/sensitivity.py --cerberus /path/to/cerberus-launcher
```

The script builds scratch copies, checks each baseline, and seeds two defects:
rejecting the valid empty-haystack search start, and suppressing the scanner's
read-error classification. Both must compile and fail assertions natively
and under Cerberus; compilation failures and checker-internal errors do not
count as rejection. Core dumps are disabled. Use the same outer memory cap
as other heavy checks.

The first CLI-only stream harness missed the scanner defect because closing
a failed `CuInput` recovered the error. A direct scanner-result assertion now
rejects that same mutant. The production code was not changed.

The serial optimization sensors additionally run through
`tests/serial_sensitivity.py`. Overstepping a uniform blocker, using a
borrow-propagating zero-byte mask, and shifting the lane sum incorrectly all
compile and fail behavioral assertions natively and under Cerberus. Full
counting/alignment cases also run in the native suite's `test_count.c`.

## Limits

This suite does not interpret the 8 MiB line boundary, 64 KiB refill-boundary
fixtures, the full four-byte-alphabet finder enumeration, large-input memory
budgets, pthread execution, or performance corpora. Those original native tests and fuzz checks
remain required. Small concrete scenarios are not a proof for arbitrary
input lengths or all allocator/I/O schedules.

The installed Cerberus revision needs the private repairs documented in
`../dependable-c-agent-toolchain/docs/checker-repairs/` relative to the cgrep
checkout. Use an explicit `DCHECK_CERBERUS` launcher with the private runtime
and a virtual `/tmp`; do not silently switch tools or skip failures. A full
unbounded Cerberus run remains available by explicitly setting
`SEMANTICS_TEST_DIR=tests` in `dcheck.conf`, but exceeds the measured local
resource limits.

On the private repaired build, the focused run completed in about 71, 4,
and 19 seconds for CLI, finder, and streaming. Peak RSS was approximately
424, 187, and 271 MiB. These are resource observations, not fixed budgets or
performance promises. Receipts are in `/tmp/cgrep-bounded-semantics.kTAzwU`.
