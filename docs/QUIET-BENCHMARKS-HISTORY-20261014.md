# Quietness admission for benchmarks

A contended wall-time result is not a quiet-host baseline. The dedicated
wrapper checks five consecutive seconds of low measured background CPU
before starting. It also monitors the whole run. The default limits are
10% background CPU and aggregate I/O wait at most 2%. Admission also requires
one-minute load divided by logical CPUs at most 0.5. During timing, load
includes the benchmark's own workers, so only background CPU and I/O wait
determine eligibility.

The wrapper reads aggregate `/proc/stat` and the dedicated cgroup's
`cpu.stat`. It subtracts that scope's CPU usage, so the benchmark's own
serial/parallel workers do not invalidate the measurement. Keep unrelated
work outside that scope. The scope must have exactly a 3 GiB memory cap
and no swap. There is no uncapped fallback.

```sh
systemd-run --user --scope -p MemoryMax=3G -p MemorySwapMax=0 nice -n 15 \
  bash -c 'ulimit -c 0; python3 -I tests/benchmark_quiet.py --report host.json -- \
    python3 -I tests/benchmark_parallel.py build/cgrep --cgrep-threads --numbers --output timings.json'
```

Exit 2 means the host did not pass admission within the bounded wait and no
benchmark started. Exit 3 means timing completed but background interference
exceeded the limits. The timing report and every observation remain intact.
A benchmark failure remains a failure. Exit 0 means the command succeeded
and all monitored intervals met the configured quietness limits.

This is CPU/load admission, not exclusive isolation, fixed CPU frequency,
thermal control, or cold-I/O proof. Retain the host report with the binary
hashes, exact-output checks, randomized samples, and fixed thread settings.
Do not silently discard a noneligible report or select the fastest thread
count from each case.

The first attempt on the current shared host did not admit a run. Its 120
samples recorded background CPU of 16.90% minimum, 31.40% median, and 98.24%
maximum. No quiet baseline is claimed. Original production C remains
unchanged while numbering alternatives are evaluated in scratch copies.

Seven-round CPU-time probes suggest that a four-word count group with a
1024-byte adaptive batch may reduce dense counting time. Those probes remain
contended, are not accepted full-search measurements, and are not production
verification. An idle host/window and paired full-output measurements are
required before promoting a candidate. Independent review was unavailable.

Local evidence is in `/tmp/cgrep-numbering-ci.9zCgJw`, including
`baseline-host.json`, the rejected-admission control, and count-only scratch
results. The original benchmark data is unchanged.
