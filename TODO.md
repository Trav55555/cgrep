# Runtime TODOs

Use cgrep to test whether better C interfaces reduce maintenance work.
These tasks complement the feature order in [README.md](README.md).
Ship each task as a separately verified slice. Add shared c-utils APIs only
when cgrep needs them.

## Establish the baseline

- [ ] Re-run correctness and performance checks against the current implementation.
  Update [verification.md](verification.md) with commands, versions, results,
  and remaining gaps. Compare absent literals, late matches, repetitive
  candidates, and many small files. Record peak memory as well as elapsed time.
  Keep historical measurements separate from new results.

## Preserve useful error information

- [ ] Introduce structured internal errors where callers need to distinguish causes.
  Distinguish open failures, read failures, allocation failures, resource-limit
  refusals, and output failures. Preserve the relevant OS error before cleanup.
  Keep the CLI's status 0/1/2 contract and continued search after input failure.
  Acceptance: inject each failure and assert its classification, cleanup,
  diagnostic, and exit status. Do not require a new result type for infallible calls.

## Bound resource use

- [ ] Add a configurable input-memory budget.
  The streaming reader now reuses a 64 KiB window, caps text lines at 8 MiB,
  and has file/pipe memory-limit tests. A configurable total-memory budget
  remains open. Define behavior for oversized inputs and lines, including matches that cross
  buffer boundaries. Specify binary detection and missing-final-newline behavior.
  Acceptance: test large files and pipes under a small memory budget, compare
  supported text behavior with ripgrep, and verify allocation-failure cleanup.

## Make task lifetimes explicit

- [ ] Prototype a scoped, bounded worker pool for independent file searches.
  Keep platform threads behind a small API. A scope owns and joins every task
  it starts. Give each worker its own temporary storage. Keep pattern storage
  alive until all workers finish, and document which data is read-only.
  Start with OS threads, not fibers or a general-purpose goroutine runtime.
  Acceptance: run with one worker and several workers, inject startup and task
  failures, and verify that no task survives scope exit.

## Transfer work without sharing mutable state

- [ ] Add bounded job and result queues when the worker prototype needs them.
  Define whether each message copies data or transfers ownership. Document
  queue closure and behavior when a producer or consumer stops. Use one output
  writer. Decide output ordering and bound buffered results, including when an
  early file is slow. Avoid blocking shutdown on a full queue.
  Acceptance: exercise empty and full queues, slow consumers, closed queues,
  worker errors, and output failures. Compare one-worker and multi-worker
  results under the chosen ordering contract.

## Cancel and shut down predictably

- [ ] Add cooperative cancellation and deadlines to the parallel-search path.
  Stop submitting work after cancellation. Wake blocked queue operations and
  join workers before releasing their storage. Preserve continued search for
  ordinary input failures. Define when output failure stops the entire search.
  Document whether blocking file I/O can exceed a deadline. Do not promise
  immediate cancellation without an interruptible platform implementation.
  Acceptance: cancel before submission, during search, and while queues are
  full. Verify cleanup, termination, and the documented CLI status.

## Check the concurrency contract

- [ ] Add deterministic scheduling tests and a separate race-detection build.
  Keep single-threaded search logic testable without the worker runtime.
  Seed a data race to verify that the race detector fails. Report unavailable
  tools as skipped checks, not passes. Keep native concurrency tests separate
  from the single-threaded wasm checks.
  Acceptance: retain a failing regression for each discovered concurrency bug
  and record exercised platforms and tool limitations in verification.md.
