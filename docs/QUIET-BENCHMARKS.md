# Quietness protocol for benchmarks

A contended timing is not a quiet-host baseline. The wrapper admits a run only
after five consecutive seconds with background CPU at most 10%, aggregate
I/O wait at most 2%, and one-minute load per logical CPU at most 0.5.
It then monitors background CPU and I/O wait throughout the command.

The wrapper subtracts the dedicated scope's CPU usage from aggregate
`/proc/stat` usage. Keep unrelated work outside that scope. During timing,
load includes benchmark workers, so load gates admission only. The cgroup
must have exactly a 3 GiB memory cap and no swap; there is no uncapped fallback.

## Run one profile

From the repository root:

```sh
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 nice -n 15 \
  bash -c 'ulimit -c 0; python3 -I tests/benchmark_quiet.py --report host.json -- \
    python3 -I tests/benchmark_parallel.py /path/to/candidate --baseline /path/to/control --cgrep-threads --numbers --repeats 21 --output timings.json'
```

Run the plain profile separately without `--numbers`, with distinct report
paths. Select the intended temporary filesystem explicitly, for example with
`TMPDIR`; the script does not choose tmpfs automatically. Keep the host report,
executable/source hashes, all fixed configurations, raw wall/child-CPU samples,
and complete-output validation together. The wrapper checks interference;
the benchmark driver checks the actual output, status and stderr.

## Interpret the result

Read the report's `admitted`, `returncode` and `quiet_eligible` fields along
with the exit code:

- Failed admission exits 2 without starting the benchmark.
- Benchmark failures take precedence over quietness. The recorded child status
  can also be 2 or 3.
- A successful benchmark with eligible intervals exits 0.
- A successful but ineligible benchmark exits 3. This includes interference
  above the limits or a command too short to record any monitored interval.

The report distinguishes admission failure from a benchmark failure. Any
benchmark failure remains a failure, regardless of quietness.

Retain failed, incomplete and noneligible observations. Do not select only
the fastest thread count or retry a busy workstation indefinitely. A required
quiet window is a measurement prerequisite, not a reason to stop unrelated
processes or relax thresholds.

This protocol measures guest/host-visible interference. It does not establish
exclusive hardware, fixed frequency, thermal control or physically cold I/O.
Keep warm explicit files, traversal, cache advice, physical I/O and terminal/
pipe output results separate.

## Current evidence

The counting/filename predecessor completed a quiet Spot comparison. The
newer batched writer has correctness acceptance but no quiet qualification;
its later Spot attempts launched no instance. See
[current performance status](PERFORMANCE.md) and the [evidence index](EVIDENCE.md).

The original workstation admission failures and scratch-counting observations
remain in the [historical snapshot](QUIET-BENCHMARKS-HISTORY-20261014.md).
That snapshot's "production unchanged" wording describes its original stage,
not the current implementation. Past cloud approvals are not standing grants.
