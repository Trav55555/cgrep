# Ordered bounded parallel search

## Ground and scope

The caller is `cgrep_main`: it prepares an immutable literal, scans explicit
paths, continues after input errors, stops after observed output failure, and
releases arenas only after work ends. `search_stream` owns no input lifetime.
Preserve the current serial stdin/non-regular behavior. This slice accepts
explicit worker counts 1 through 8, separated or joined with `-j`; default
selection remains serial until the measured behavior justifies automatic use.

## Alternatives and decision

1. Per-worker kernel pipes fit the existing FILE sink but require descriptor
   ownership and internal SIGPIPE handling during cancellation. Pipe capacity
   is platform-managed and producer wakeup needs careful closure sequencing.
2. Per-worker memory channels avoid internal signals. Publication,
   ordered consumption, cancellation, and slot reuse share one pool mutex.

Choose memory channels with a direct scanner writer callback. The initial
`fopencookie` adapter regressed output-heavy workloads and crossed an
uninstrumented libc buffer boundary that MSan could not track. Replacing it
keeps channel copies instrumented, without diagnostic suppression. Both sinks
now use the same decimal-prefix formatter and preserve the same output bytes. The Linux/glibc >= 2.34 backend retains
C89 syntax. Compiler feature defines must precede the
forced poison header, which already includes system stdio. Portable targets
retain serial search. No system package or published artifact changes.

## Shape

```text
Search
  immutable Literal
  per-input Finder (learned filter choice), owned by each scanner
  serial FILE output or direct writer callback
  optional cancellation predicate + worker context

Pool
  mutex, per-worker conditions, cancellation flag
  immutable path list, job count
  one Worker per started job (allocated for jobs, not the maximum)

Worker
  persistent pthread, parent Pool pointer
  scans paths first, first + jobs, ... without coordinator assignment
  two 1 MiB output slots: FREE (worker-owned) or CHUNK / DONE / SERIAL
  fill index (worker) and drain index (coordinator) alternate slots
  private reusable input arena/window in worker routine

parallel_search(search, paths, count, jobs, out, err, status)
  return not-selected for stdin or unsupported backend
  allocate pool and workers, start scope
  consume path i from worker i % jobs, draining its slots in alternation
  free each slot after writing it; the worker may then refill it
  cancel on output failure, wake blocked publishers, join every started task
  free slots only after joins
```

Only the coordinator writes caller stdout/stderr. A slot's bytes are worker
owned while FREE and coordinator owned while CHUNK, DONE or SERIAL. A worker
publishes a full slot and continues in its other slot once that is free, so
it can search one file ahead of consumption. Because the worker fills and the
coordinator drains slots in the same alternation, argument order holds
without per-file assignment. If a worker's drain slot is free, nothing is
published, so its fill slot is also free: the two cannot wait on each other.
Terminal publication includes remaining bytes with the result, avoiding a
separate tiny-file handoff. The coordinator snapshots state and length under
the mutex, releases it for output I/O, then reacquires it before freeing the
slot. Ordinary input failures do not cancel other jobs. Workers check
cancellation every eighth input refill. After cancellation a worker publishes
nothing, since its fill slot may still be held. A blocking regular file read
can delay a join; instantaneous interruption is not promised.

Measured on 16 x 4 MiB mixed-text files with about 1.5 MiB of output each,
64 KiB single channels left `-j8` slower than serial; 256 KiB slots took
27.6 ms and 1 MiB slots 13.7 ms (see
[the performance review](PERFORMANCE-REVIEW-20261014.md)). Eight workers
reserve 16 MiB of slots, which the kernel commits only as output fills them.

The worker opens with nonblocking flags and verifies the descriptor is regular.
A raced non-regular path requests serial handling only when the coordinator
reaches that argument. This prevents a prefetched FIFO from trapping shutdown.
Worker-owned FILE inputs attach a borrowed CuInput, detach it, then close the
FILE explicitly, preserving the scanner's first failure.

## Synchronous fragment batches

The unpublished writer sends one record's borrowed fragments through one
callback, at most five for a text line or three for a binary warning. The
callback consumes descriptors and data before returning; stack prefixes and
warnings never escape. It still reports zero on failure and stops scanning.

A preflight checks each length against `room - total` before adding it. If the
whole record fits, the worker copies nonempty fragments and updates its used
length once; one-byte fragments use direct stores. An exact fill remains
private until the existing lazy publication or terminal completion. If any
fragment does not fit, the original splitting/publication loop handles the
entire record without prior writes. Zero-length fragments may have null pointers.

This changes local copying only. Channel ownership, coordinator locking,
ordered acknowledgment, output flushing, cancellation, joins, and capacity
remain unchanged. Boundary/failure fixtures and compiled mutants exercise the
new paths; [verification](../verification.md) records the full acceptance.

## Verification gates

- Exact serial/parallel output order and diagnostics; fixed literal parity.
- Startup failure before inputs, partial startup cleanup, missing/read/close
  failures, later-input continuation, output failure, blocked channel wakeup,
  raced non-regular fallback, and no task surviving return.
- Native sanitizer/analyzer/release/Valgrind profiles and a live race detector
  where supported; portable argument parsing and serial behavior under wasm.
- Paired strongest-competitor benchmarks, including heavy output and small
  files; do not promote automatic selection without evidence.

`make verify` passes under the 3 GiB/no-swap scope, including required native
TSan/ASan worker tests, ordered differential checks, resource limits, and
compiled ordering/status/cancellation mutants. A barrier-started negative
race control must fail 3/3 times. A single tiny unsynchronized write proved
schedule-sensitive and was replaced, not accepted as a working detector.
Portable writer and option cases also run under wasm and bounded Cerberus;
Cerberus does not interpret pthreads. See [verification](../verification.md) for receipts.

The initial independent architecture-review provider was unavailable. Later
read-only scout reviews found no actionable batching issue but did not rerun
the gates. This is an executed-test plan and local review, not a concurrency
proof or a substitute for independent architecture review.
