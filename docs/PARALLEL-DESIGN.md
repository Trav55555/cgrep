# Ordered bounded parallel search

## Ground and scope

The caller is `cgrep_main`: it prepares an immutable literal, scans explicit
paths, continues after input errors, stops after observed output failure, and
releases arenas only after work ends. `search_stream` owns no input lifetime.
Preserve the current serial stdin/non-regular behavior. `-j` accepts 1
through 32, separated or joined. The default was serial until the batched
scheduler below was measured; it is now one worker per online CPU, at most
32 (see [Default worker count](#default-worker-count)).

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

Listing
  the path arguments, which are directories, walk options, ignore stack
  Inputs: shared path/walked vectors (touched under the pool mutex),
  a pending run of up to 64 entries the lister publishes at once

Pool
  mutex, pool condition (inputs published / batch claimed / listing done)
  cancellation flag, allowed and started worker counts
  published input count, claim index, listed flag
  ring of claimed batches (worker, start, count) in claim order
  walker diagnostics: a memory stream copied under the mutex
  up to 32 Workers, each allocated when it starts

Worker
  persistent pthread, parent Pool pointer, own condition
  claims a batch of consecutive inputs once its fill slot is free
  two 1 MiB output slots: FREE (worker-owned) or CHUNK / DONE
  each slot records the inputs whose output ends in it: path, end
  offset, ScanResult, serial flag (non-regular: coordinator searches it)
  next claim size, doubling while inputs stay small
  private input arena/window and a 64 KiB stdio buffer

parallel_search(search, listing, jobs, in, out, err, status)
  return not-selected when the thread backend is unavailable
  list arguments inline, or start a walker thread for directory arguments
  loop: print published walk diagnostics; start workers while the
        unclaimed backlog is 16 per worker; drain the head batch's slots
        in alternation, writing each record's bytes then its result;
        with no worker started, search the next input inline
  cancel on output failure, wake every waiter, join walker and workers
  free slots only after joins
```

Only the coordinator writes caller stdout/stderr. A slot's bytes and records
are worker owned while FREE and coordinator owned while CHUNK or DONE. A
worker publishes a full slot as a CHUNK and continues in its other slot once
that is free, so a batch's output may span many slots; the batch ends with a
DONE slot. The coordinator consumes batches in claim order, which is input
order, so argument order holds without per-file assignment. A worker claims
only with a free fill slot, so the head batch's worker never waits on
anything but the coordinator, and the coordinator never waits on a worker
that is not the head: no cycle. The coordinator snapshots the slot state
under the mutex, releases it for output I/O, then reacquires it before
freeing the slot. Ordinary input failures do not cancel other jobs. Workers
check cancellation every eighth input refill and between inputs of a batch.
After cancellation a worker publishes nothing, since its fill slot may still
be held. A blocking regular file read can delay a join; instantaneous
interruption is not promised.

## Batched claiming and streamed listing

The first pool assigned input k to worker k mod jobs and handed over every
file separately, with a slot wake-up per file in each direction. On 100,000
one-line files that cost more than three futex calls per file, and the
per-file open also paid an fstat and an fcntl that the serial path does not.
Workers were started for every run, even one of two files.

Now:

- A worker claims a contiguous batch under the mutex: at first one input,
  doubling after each batch while the batch's average input keeps a claim
  under 4 MiB (`next_batch`), at most 64. Once the listing is complete a
  claim is also at most the unclaimed share per running worker
  (`claim_size`), so the tail balances. The whole batch is one handoff.
- Each input's end offset, result and path are recorded in the slot where
  its output ends, so the coordinator writes bytes and reports results and
  non-regular fallbacks per input exactly as before. stdout bytes, stderr
  text and the exit status are unchanged at every `-j`.
- A walked input was a regular file when listed, so the worker trusts that
  and skips the fstat; the nonblocking open still keeps a raced FIFO from
  blocking (its read fails instead). Arguments are classified as before. A
  64 KiB `setvbuf` buffer set before the first read removes glibc's sizing
  fstat. A small file now costs open, fcntl, read, read, close: the serial
  path's count.
- Directory arguments are listed on a walker thread (1 MiB stack, for the
  256-level nesting limit) that publishes inputs in runs of 64 and at the
  end, and stops listing once it sees cancellation. Workers wait on the pool
  condition when they catch up with the walk. The walker's diagnostics go to
  a memory stream it copies into the pool under the mutex; the coordinator
  prints them whenever it holds the mutex and after the joins, so their
  position relative to search output can differ from `-j1`.
- Workers start lazily: the coordinator starts one more while fewer than
  the unclaimed backlog divided by 16 are running (at most `-j`), and
  searches inputs itself while none has started. A worker that cannot be
  allocated or started is reported (status 2) unless the backend reports
  ENOSYS, and the search continues with the workers that exist. Workers are
  allocated as they start: an unused `-j32` costs nothing.

Measured on 12 logical CPUs, median of 9 warm runs, wall / CPU in ms,
before (fixed lanes) and after (batches), both with explicit `-j`:

| Workload | before -j1 | before -j8 | after -j1 | after -j8 |
| --- | ---: | ---: | ---: | ---: |
| 100,000 one-line files, no match | 513 / 512 | 208 / 1161 | 509 / 509 | 107 / 611 |
| 9,331 directories x 4 small files | 245 / 245 | 122 / 487 | 244 / 244 | 68 / 271 |
| cgrep source tree, `return` | 4.1 / 4.1 | 3.9 / 6.1 | 4.2 / 4.2 | 3.9 / 5.7 |
| /usr/include, `int` | 32.5 / 32.4 | 13.0 / 57.7 | 33.1 / 32.9 | 6.2 / 38.0 |
| one small file | 0.50 / 0.46 | 0.48 / 0.44 | 0.49 / 0.45 | 0.49 / 0.45 |
| ten small files | 0.55 / 0.52 | 0.77 / 1.00 | 0.59 / 0.53 | 0.58 / 0.52 |

`strace -f -c` on the 100,000-file search at `-j8`: 300k futex calls and
200k fstat before; 7.6k futex, no fstat, 100k fcntl after (508k calls in
all against 500k at `-j1`).

### Default worker count

The default changed from 1 to the online CPU count (at most 32) because,
with that default (12 here, median of 9 and of 21 warm runs for the small
cases), every condition held: one small file 0.43 ms against 0.45-0.50 at
`-j1` and ten small files 0.48 against 0.46-0.47 (both inside 5%, since
neither starts a worker); the 100,000-file directory 101 ms against 507;
the 9,331-directory tree 68 against 245; the cgrep source tree 3.9 against
4.1-4.5; and /usr/include 5.6 against 30.5. CPU time rises with workers
(680 ms against 507 on the flat directory), which is the price of the wall
time.

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
- Startup failure before inputs, a worker failing to start mid-run, missing/
  read/close failures, later-input continuation, output failure inside a
  batch, blocked channel wakeup, raced non-regular fallback (also mid-batch),
  a large file inside a batch, the claim and start policies, a walk that
  continues after its first inputs are consumed, walk diagnostics through
  the coordinator, listing without a memory stream, a cancelled walk that
  stops listing, and no task surviving return.
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
