# Performance review, 2026-10-14

A critical review of cgrep's performance, with c-utils reviewed separately
in `../c-utils/docs/PERFORMANCE-REVIEW-20261014.md`. Sources: a code-level
review with scratch-built variants, a digest of the Brain-2 search notes,
and web research into ripgrep, the memchr crate, GNU grep, ugrep,
hypergrep and Hyperscan. All timings came from a busy workstation (Ryzen 5
7600, tmpfs, `LC_ALL=C`) and are indicative only. Every scratch variant
produced byte-identical output to the build it was compared with.

## The benchmarks hid the largest cost

Every fixture in `tests/benchmark_parallel.py` repeats one 23-byte line,
`alpha beta gamma delta\n`. `NEEDLE42` anchors on `2`, which never occurs,
and `alpha` matches every line, so no fixture ever produced a false
candidate. The quiet Spot qualification and the Callgrind comparisons
measured a workload that cannot reach the candidate-rejection path.

On mixed text (word-salad log lines, mean 73 bytes, about 10% matching
`error`), serial cgrep was 3-8x slower than `rg -F -j1`:

| Case (64 MiB) | cgrep | rg -j1 |
| --- | ---: | ---: |
| Heavy output, `error` | 72-93 ms | 15-18 ms |
| No match, `e 5` | 81-113 ms | 10 ms |
| No match, `kernel mutex` | 42-59 ms | 10 ms |
| No match, `zebrafish` | 13.6 ms | 10.3 ms (mmap) |

## Findings

### F1. The KMP fallback is a cliff

`find()` reset its reject counters on every call, and `search_text` calls
it once per matching line, so learned filter choices were discarded at
each match. After eight false candidates it switched anchor once; after
eight more it ran scalar KMP until the next match and never returned to
the `memchr` filter. Callgrind attributed 74% of heavy-output instructions
to `find_end`. Disabling the fallback (diagnostic only) cut heavy output
from 93 to 40 ms and `e 5` from 113 to 22 ms.

A linear bound does not need an unbounded KMP tail. Charging each failed
confirmation against the bytes the filter has skipped, running KMP only
over a bounded window when that budget trips, then resuming the filter,
keeps total work O(input + pattern) while staying on the fast path for
ordinary text.

### F2. The anchor came from the pattern, not the input

The anchor was the byte least repeated within the pattern, with ties going
to later offsets: `o` for `error`, `h` for `zebrafish`, `5` for `e 5`.
The pattern's own histogram says nothing about the haystack. ripgrep and
the memchr crate rank bytes with a static 256-entry background-frequency
table.

### F3. 64 KiB output channels serialized `-j` heavy output

A non-head worker that fills its 64 KiB channel sleeps until the
coordinator has drained every earlier file. In the Spot run, `-j8` heavy
output (36.69 ms) was slower than serial (18.35 ms). A scratch variant with
1 MiB channels ran 8 x 8 MiB heavy files in 24.5 ms at `-j8` versus 40 ms
with 64 KiB, and futex calls fell from 973 to 144. ripgrep `-j8` prints
files in completion order, so 36.69 vs 14.81 ms compared ordered with
unordered output.

### F4. Assignment was coupled to in-order consumption

A worker received path `i + jobs` only after the coordinator consumed
path `i`. Workers that finished early sat idle; one slow file stalled the
other seven. 2000 tiny files at `-j8` made 13,220 futex calls and ran
slower than serial.

### F5. One `write` per matching file

The serial loop and the coordinator flushed stdout after every file.
For 2000 tiny files that was 2000 `write` calls, 14.7% of syscall time.
Errors are still observable through `ferror` and the final flush.

### F6. `-n` rarely used the word-lane counter

The word-lane counter engaged only after four consecutive line gaps under
64 bytes. Typical 70-120 byte lines fell back to one `memchr` per line,
about 20% of a numbered run. ripgrep and GNU grep both count lazily up to
the printed line, as cgrep already does; the difference was throughput.

### F7. Smaller costs

- Cancellation took the shared pool mutex on every 64 KiB refill.
  Plausible cause of the medium late-match `-j8` regression; not isolated.
- Every parallel path was `stat`-ed before the pool started and again by
  the worker.
- Serial output used up to four locked stdio calls per line. Without
  names or numbers, consecutive matching lines are one contiguous slice.

### Measured and not worth changing

- `fread` issues two `read` calls per 64 KiB window; unbuffered input
  halved the syscalls but not the time (the page-cache copy dominates).
- `-O3 -march=native` and LTO only sped up the KMP path.
- Startup: one tiny file runs at the `/bin/true` floor.
- The "tiny files are slow" gap did not reproduce: 2000 files took 12.3 ms
  serial versus 14.3 ms for `rg -j8`.
- mmap gained ripgrep about 2 ms per 64 MiB here, and truncating a mapped
  file raises SIGBUS, which conflicts with cgrep's error contract.

## Techniques from the research

| Technique | Evidence | Fit for cgrep |
| --- | --- | --- |
| Packed pair: SIMD filter on two rare needle bytes | memchr crate 40.2 GB/s vs glibc memmem 9.6 GB/s (rebar) | SSE2 is baseline on x86-64; keep scalar fallback |
| Static byte-rank table | ripgrep, memchr `DefaultFrequencyRank` | 256-byte table, portable |
| Prefilter shut-off: 50 calls, mean skip < 8 bytes | memchr `PrefilterState` | Same idea as a work budget |
| SIMD newline count, lazy | ripgrep `lines::count`, GNU `nlscan`, bytecount | SSE2 `pcmpeqb` + `psadbw` |
| Coalesced output from the input buffer | ripgrep `sink_fast`, Haertel | Runs of plain matching lines |
| mmap for few large files | ripgrep: ~25% faster single file, 5x slower on many | Rejected, see above |
| io_uring, `posix_fadvise`, `pcmpestri`, Teddy | No grep evidence, or multi-pattern only | Skip |

Benchmark warnings: GNU grep stops at the first match when stdout is
`/dev/null`; on one file ripgrep runs serially with mmap.

## Disposition

Implementation status of each finding is tracked in
[PERFORMANCE.md](PERFORMANCE.md).
