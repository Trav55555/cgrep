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
2. Per-worker 64 KiB memory channels avoid internal signals. Publication,
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
  serial FILE output or direct writer callback
  optional cancellation predicate + worker context

Pool
  mutex, per-slot conditions, cancellation flag
  fixed maximum of eight Workers

Worker
  persistent pthread, parent Pool pointer
  assigned borrowed path
  WAIT / ASSIGNED / RUNNING / CHUNK / DONE / SERIAL state
  ScanResult, 64 KiB output channel, used count
  private reusable input arena/window in worker routine

parallel_search(search, paths, count, jobs, out, err, status)
  return not-selected for stdin/non-regular paths or unsupported backend
  initialize and start scope before assigning inputs
  assign at most jobs paths
  consume slots in argument order, then reuse each for its next path
  cancel on output failure, wake blocked publishers, join every started task
  free channels only after joins
```

Only the coordinator writes caller stdout/stderr. A channel's bytes are worker
owned while RUNNING and coordinator owned while CHUNK or DONE. Terminal
publication includes remaining bytes with the result, avoiding a separate
tiny-file handoff. Publication/acknowledgment uses the mutex and conditions.
The coordinator snapshots state and length under the mutex, releases it for
output I/O, then reacquires it before acknowledging or assigning another
input. A worker cannot reuse transferred bytes during that I/O. Ordinary input failures do not cancel
other jobs. Cancellation is checked before each input refill. A blocking regular
file read can delay a join; instantaneous interruption is not promised.

The worker opens with nonblocking flags and verifies the descriptor is regular.
A raced non-regular path requests serial handling only when the coordinator
reaches that argument. This prevents a prefetched FIFO from trapping shutdown.
Worker-owned FILE inputs attach a borrowed CuInput, detach it, then close the
FILE explicitly, preserving the scanner's first failure.

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

The independent architecture-review provider was unavailable. This is a local
sketch and executed-test plan, not an independently reviewed concurrency design.
