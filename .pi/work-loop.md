# Current work state

Documentation/evidence organization and recorder cleanup are complete.
Recorder hash/receipt checks are shared; permanent rejection tests protect
their behavior. C code, benchmark tooling, raw reports, installed binaries
and Git history are unchanged.

## Candidate and remaining limits

- The unpublished batched writer passed isolated `make verify`, receipt `verify.2MT7um`: 12,000 ordered comparisons, required sanitizer/race/error/budget checks and bounded portable semantics. Coverage was 91.59% cgrep / 91.78% dependency. Native pthreads remain outside Cerberus semantics.
- Performance is unqualified. The predecessor's quiet comparison showed specific gains and a medium plain late-match `-j8` regression; it does not qualify the current writer. Busy-host instruction savings are not native speedups.
- Same-type and approved alternate Spot campaigns launched no instance. Task resource cleanup was separately checked at the recorded timestamps. Provisioning stopped after the promised final attempt. No cloud task remains active.
- CI artifact collection/retrieval passed locally; live upload/download/retention remains unverified and unpublished.

## Recorder checks

`tests/test_recorders.py` runs from `tests/verify.sh`. It uses synthetic fixtures,
not historical files, for the five rejection controls, valid/failed/skipped
receipts across all three recorders, source drift and failed-CLI output retention.
Focused tests and six disabled-guard controls passed. Old/new report data match;
the real output-batch record matches except its capture timestamp. No full C
acceptance rerun. Historical source manifests remain strict and require the
matching frozen source snapshot, including the original recorder/verification files.

## Next decision and boundaries

Quiet qualification comes before another optimization. A new host/retry needs
a new operational decision. Earlier Spot approvals are exhausted; no higher
bid, on-demand fallback, region expansion or automatic retry is authorized.
A workstation retry also needs materially changed quietness conditions.

Keep heavy work sequential in dedicated 3 GiB/no-swap scopes at nice 15 with
core dumps disabled. The 2026-10-14 review fixes replaced the fixed 64 KiB
channels with two 1 MiB slots per worker at the user's request; see
docs/PERFORMANCE-REVIEW-20261014.md. Preserve synchronous borrowed
fragments, ordered output/diagnostics, flush/error boundaries, continued
searching after input failures, raced-FIFO safety and cancellation/joins.

Preserve unrelated `tests/benchmark.py`, `tests/test_benchmark.py` and sibling
`c-utils/src/cu_arena.c` edits. Retain failed/partial samples, hashes, receipts,
source snapshots and frozen binaries. Installation, commit/push, runner or
credential changes and live-CI publication require separate authorization.

## Context on demand

- For executable identities, report limits and raw locations, read the [evidence index](../docs/EVIDENCE.md).
- For the current target, measurements and benchmark recipes, read [performance status](../docs/PERFORMANCE.md) and the [quietness protocol](../docs/QUIET-BENCHMARKS.md).
- Before changing correctness gates, read [verification](../verification.md).
- For completed task chronology and historical approvals only, read the [byte-preserved work-loop snapshot](work-loop-history-20261014.md). Its old active plans are not current instructions.
