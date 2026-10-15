# Performance target and current status

The target is to meet the strongest valid GNU grep or ripgrep competitor on
representative literal searches without weakening correctness, ordered output,
error handling, linear search bounds, or memory limits. The target is unmet.
Default execution remains serial; worker selection is explicit.

## Current candidate: review fixes of 2026-10-14

The [performance review](PERFORMANCE-REVIEW-20261014.md) found that every
earlier fixture repeated one line that never produced a false candidate.
`tests/benchmark_parallel.py` now adds three varied-text workloads
(`mixed-heavy`, `mixed-common-anchor`, `mixed-rare-absent`). Building on
the unpublished batched writer (see the
[historical snapshot](PERFORMANCE-HISTORY-20261014.md)), the candidate
working tree implements:

| Finding | Change |
| --- | --- |
| F1 KMP cliff | Work budget against bytes skipped; one learned switch; bounded KMP windows, then the filter resumes; state persists per input |
| F2 pattern-local anchor | Static background byte rank (memchr crate table) |
| Packed pair | Two-byte filter: libc `memchr` first, SSE2 pair scan once hits prove dense |
| F3 64 KiB channels | Two 1 MiB slots per worker, allocated per started job |
| F4 coupled assignment | Workers scan `w, w + jobs, ...` one file ahead of consumption |
| F6 `-n` counting | SSE2 newline count; portable counter unchanged |
| F7 | Cancellation polled every 8th refill; no pre-pass `stat`; `fwrite_unlocked`; adjacent plain matches written as one slice |

Not changed, with reasons:

- **F5, per-file flush.** `io_test.py` checks the documented promise that an
  output failure stops before a later blocking input is opened. glibc
  already skips the `write` for files without output.
- **Coordinator `write(2)` from slots.** With 1 MiB slots, glibc's `fwrite`
  already bypasses the 64 KiB stdout buffer for the bulk of each slot.
- **`read(2)`, mmap, `posix_fadvise`, compiler flags.** No measured gain, or
  mmap's SIGBUS on truncation conflicts with the error contract.

Busy-workstation medians, 7 randomized rounds, ms, `LC_ALL=C`, complete
output to tmpfs files, against the pre-change working tree:

| Workload | Before | After | Before -j8 | After -j8 | rg -j1 | rg -j8 | GNU grep |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| mixed-heavy | 77.8 | 43.6 | 100.0 | 17.1 | 47.1 | 18.7 | 73.8 |
| mixed-common-anchor | 74.5 | 16.4 | 21.5 | 6.6 | 16.6 | 7.7 | 21.7 |
| mixed-rare-absent | 13.4 | 13.4 | 13.4 | 13.6 | 13.3 | 13.5 | 35.7 |
| matching-output-heavy | 18.1 | 18.9 | 19.9 | 9.0 | 31.4 | 11.8 | 23.2 |
| small-late-match | 10.0 | 9.8 | 10.5 | 6.0 | 17.1 | 8.9 | 9.9 |
| single-large-absent | 13.1 | 12.9 | 12.6 | 13.0 | 12.7 | 12.1 | 14.3 |
| mixed-heavy, `-n` | 92.3 | 49.2 | 100.8 | 17.3 | 62.0 | 20.0 | 91.5 |
| single-large-absent, `-n` | 18.9 | 14.2 | 18.8 | 14.4 | 12.7 | 13.5 | 25.2 |

A 21-round recheck of the medium `-j4`/`-j8` cases, which looked slower in
one sweep, showed the candidate faster in all six. Raw reports are in the
session scratchpad, not the repository. The table predates two changes the
acceptance gate required. The SSE2 code now uses GCC/Clang vector types
instead of `<emmintrin.h>`, whose definitions the project lint rejected;
the inner loops compile to the same instructions. One-byte parts no longer
use `putc_unlocked`, a glibc inline that reads `FILE` fields MemorySanitizer
cannot see initialized. In 15-21 paired rounds that cost 1.6 ms on
`mixed-heavy` (38.9 to 40.5 ms) and `matching-output-heavy` (16.3 to
17.9 ms); absent and common-anchor cases were unchanged. These are not quiet-host results,
and the candidate is neither installed nor published. Its acceptance run is
recorded in [verification](../verification.md).

## Last quiet comparison belongs to the predecessor

The counting/filename predecessor, not the current batched writer, completed
21 randomized rounds on one c7i.2xlarge Spot host. All 4,032 timed invocations
validated complete output, status and stderr after the wall timer stopped.
All 16 fixed configurations and unsuccessful preliminary attempts remain in
[spot-qualification.json](spot-qualification.json).

That comparison showed specific gains over the installed baseline: about 11%
for serial dense numbering and 28% plain / 21% numbered for serial heavy output.
It also showed a medium late-match plain `-j8` regression of about 8.7% wall
and 7.4% aggregate child CPU. Heavy plain output at eight workers still took
36.69 ms versus ripgrep's 14.81 ms. These are host/workload-specific results,
not universal nonregression or strongest-competitor parity.

The measurements used warm explicit files and complete regular-file output
sinks on tmpfs. They do not establish traversal, physically cold I/O, pipe,
terminal, workstation, fixed-frequency or physical-isolation performance.
The separate profiling phase was not independently quiet-qualified. Traced
waits and sampled user-space shares do not account for all blocked/kernel time.

For the full tables, profile recovery and earlier experiments, read the
[historical snapshot](PERFORMANCE-HISTORY-20261014.md). Its "current", "now"
and next-step statements describe their original experiments, not this page.

## Reproduce a comparison

Run each plain/numbered profile sequentially inside a dedicated 3 GiB/no-swap
scope at nice 15 with core dumps disabled. Follow the
[quietness protocol](QUIET-BENCHMARKS.md); an ordinary benchmark invocation
alone does not qualify the host.

```sh
python3 -I tests/test_parallel_benchmark.py
python3 -I tests/benchmark_parallel.py /path/to/candidate --baseline /path/to/control --mib 64 --repeats 21 --cgrep-threads --output plain.json
python3 -I tests/benchmark_parallel.py /path/to/candidate --baseline /path/to/control --mib 64 --repeats 21 --numbers --cgrep-threads --output numbered.json
```

Keep both executable hashes, source identities and every configuration's raw
wall and aggregate child-CPU samples. Compare identical complete output with
GNU grep and ripgrep `-j1/-j2/-j4/-j8/default`; retain all fixed cgrep settings.
Only ripgrep may reorder files. Validate every timed byte, status and stderr
outside the wall timer. Retain failed and noneligible observations.

The fixture sizes come from the benchmark driver. To reproduce a tmpfs
result, set `TMPDIR` to the intended tmpfs directory. The script does not
choose that filesystem automatically. `LC_ALL=C` is the default profile;
label other locales separately.

Mixed/uneven and copied-repository fixtures live in
`tests/benchmark_realistic.py`. Keep their file/pipe and cache-advice results
separate from warm explicit-file timings. Targeted cache advice is not proof
of physically cold I/O, and copied ASCII paths do not measure traversal or
original-path costs.

## Next decision

Obtain a quiet comparison of this candidate, including the varied-text
workloads, before another optimization. Remaining equal-worker gaps are
single large files (ripgrep maps them and runs serially) and serial heavy
output. Keep output/flush error boundaries, ordered diagnostics and
cancellation/join ownership intact.
