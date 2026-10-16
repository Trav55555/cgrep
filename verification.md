# Literal-search verification

Streaming input, the selective matcher, and optional line counting pass
the native and wasm gates. Boundary, memory-budget, error, and fuzz checks
support the implementation. Deep checks pass with private repaired tools and
an explicitly bounded Cerberus suite. Full-corpus semantics verification
remains incomplete because the large suites exceed local resource limits. Mismatch-guided selection closes the measured
two- and three-hot-byte gaps. The tested corpus is not evidence of universal
performance parity or a formal correctness proof.

## Unpublished 2026-10-16 ignore-file verification

Full `bash tests/verify.sh` passed on the corrected source in
`build/ignore-handover.lr3RBB/acceptance-source/cgrep`, using the adjacent
c-utils source snapshot. The script retained its systemd scope with
`MemoryMax=3G`, `MemorySwapMax=0`, nice 15, and no core dumps. The
ignore-owned inputs and dependency source match the working tree. Hashes
are in `build/ignore-handover.lr3RBB/source-hashes.json`. The copies avoid
sharing checker output with another session running in c-utils.

Concurrent edits later appeared in `src/parallel.inc`, `tests/test_parallel.c`,
`tests/parallel_check.py`, and `ci/dependencies.json`. Those edits were left
untouched. The receipt qualifies the ignore candidate against the captured
parallel and CI baseline, not the combined live tree. The differing hashes
are in `build/ignore-handover.lr3RBB/concurrent-drift.json`. Combined-tree
acceptance must follow those concurrent edits.

The receipt is
`build/ignore-handover.lr3RBB/acceptance-source/cgrep/build/verify.pGEFk5`;
the complete log is `build/ignore-handover.lr3RBB/verify-selector-final.log`.
This run includes the final exact-buffer tests, comment-only cleanup, and
all 14 mutation-selector boundary cases.
Required subset, lint, compiler matrix, analyzer, ASan/UBSan, MSan, TSan,
release, wasm, i386, Valgrind, fuzz, and bounded-semantics checks passed.
Cgrep line coverage is 90.26%, above the unchanged 90% floor.
Dependency coverage is 91.85%. The cgrep percentage does not separately
report `ignore.inc` and is not an ignore-module coverage percentage.
Authority, regions, and absent CBMC proofs remain explicit cgrep skips.

The native worker controls, framing/serial mutants, and I/O budgets passed.
All four ordinary comparisons passed 3000/3000 cases, seeds 7 and 42 at
`-j1` and `-j8`. Ignore comparisons passed 1000/1000 with seed 7 at `-j1`
and seed 42 at `-j8`. `make check` was not run separately.

Additional executed checks:

- `make -j2 all test` passed with the captured dependency, then passed
  again with the default sibling dependency. Native output is in
  `native-complete.log` and `native-default-final.log` under the receipt
  directory. The dependency source hashes match.
- `python3 -I tests/ignore_difftest.py build/cgrep --cases 1000 --seed 1`
  passed 1000/1000; the same command with seed 7, and seed 42 with
  `--jobs 8`, also passed 1000/1000 each. The original seed-1, 26-case
  reproducer passed 26/26 on the final native CLI.
- `dcheck fast src/cgrep.c` and `dcheck fast tests/test_ignore.c` passed
  subset and lint.
- A targeted `-Wshadow` rebuild and execution of `test_walk` and
  `test_parallel` passed after renaming the new shadowing locals.

The new walk cases cover escaped `#`, `!`, `*`, and trailing spaces;
CRLF and unterminated ignore lines; and leading, intermediate, and
trailing `**`. Other cases cover directory-only rules against files,
child negation under a pruned directory, unreadable ignore files,
relative roots, `$HOME` global-file fallback, and ordered `-j8` output.
Permission cases run when the host actually denies access. WASI skips
the permission tests.

`tests/test_ignore.c` adds exact-size heap buffers for glob and parser
boundaries, final unterminated lines, blank rules, negated classes, and
initial escapes. It injects allocation failure with `cu_arena_fail_at`.
Synthetic stacks exercise the outermost repository, anchored global
patterns, leading separators, and the ancestor span boundary. These are
matcher tests, not proof of OS support for paths longer than 4095 bytes.

### Failures found and fixed

The first completed acceptance attempt failed subset checking because two
new `joined` locals shadowed the parallel test's counter. It also failed
coverage at 89.82%. The locals were renamed, and relative-root and HOME
fallback tests exercised previously untested branches. The failed receipt
and coverage log are retained as `deep-before.json` and
`coverage-before.log` under `build/ignore-handover.lr3RBB`.

The first mutation baseline failed before testing any mutants. Replaying
its command with visible output exposed seed 1, case 25: an ancestor rule
matched only the root prefix plus the basename, losing intermediate
directories. A minimal parent rule `r/sub/x` therefore failed to exclude
`r/sub/x` when searching `r`.

Ancestor matching also incorrectly stripped a hidden name's leading dot
for a search root of `.`. The new regression
failed before the fix and passed afterward. Ancestor rules now receive
the full path below the search root, with dots preserved. The baseline
log and original generated fixture are retained under the same receipt
directory. Earlier shared-checkout and pre-fix acceptance attempts were
stopped. They are not passing acceptance evidence.

A separate CLI probe found that bracket classes consumed backslashes as
escapes, unlike ripgrep. The matcher now treats class backslashes as
literal bytes and rejects reversed ranges. `class-before.log` records
the old matcher failing the new walk assertion. The corrected walk suite
passes. Malformed-pattern diagnostics remain outside compatibility.

### Ignore mutation campaign

`make mutate` ran against `src/ignore.inc` in the isolated mutation
source copy, with two workers and an outer 3 GiB/no-swap scope. The first
completed module campaign used `MUTATION_TIMEOUT=60` and the walk suite
in native, ASan/UBSan, and MSan builds. Both differential suites ran 150
cases. It classified 318 variants as 264 killed, 52 surviving, and 2
invalid. Raw output is `mutate-scoped.log` under the receipt directory.

After adding `test_ignore`, the signature-filtered runner `survivors.py`
replayed all 52 survivors with `MUTATION_TIMEOUT=90`. It rejected 36 and
left 16. A final replay of those 16 rejected 3 and left 13. Those logs
are `mutate-survivors.log` and `mutate-final.log`. The module source did
not change between these campaigns. The later commands added tests and
retained the walk suite and both comparisons. They did not remove gates.

The aggregate for the 318 distinct variants is **303 rejected, 13
surviving, and 2 invalid**, or 95.9% rejection among valid variants.
There were no runner-wide timeouts. A rejected command can include a
strict-build failure or a per-test deadline, not only an assertion.
The last two campaigns are reruns, not additional distinct variants.
`make` exits 2 because the underlying runner exits 1 for survivors.
The broad default campaign was stopped without a score. No completed
mutation campaign for all of `src/cgrep.c` is claimed here.

The following arguments cover all 13 remaining variants. Line and column
numbers identify the `src/ignore.inc` snapshot in the raw mutation logs.

| Location and change | Count | Argument |
| --- | ---: | --- |
| 73:14 and 74:14, initial `star_g` or `star_s` from 0 to 1 | 2 | Both values are assigned when a star sets `have_star`. Without that branch, neither initializer is read. |
| 106:29, range guard `k + 2 < gl` to `k + 3 < gl` | 1 | A valid endpoint at `k + 2` requires a closing bracket at or after `k + 3`. Thus `k + 3 < gl` is already true for every valid range. An unclosed class fails either way. |
| 116:23, closing check `<` to `<=` | 1 | The extra case has no closing bracket. It advances `gi` to `gl + 1`, which cannot pass the final `gi == gl` check. Backtracking retries the same unclosed class. No byte is read through that out-of-range index. |
| 227:21, parse loop `<` to `<=` | 1 | The added iteration has zero remaining bytes and skips an empty rule. Empty file reads use the non-NULL empty-string sentinel. Loaded lengths remain below the allocation capacity, so the final increment does not wrap. |
| 244:26, 244:29, 244:41, 244:63, and 244:83, suppress leading escaped `!` or `#` stripping | 5 | Comment and negation decisions happen before this branch. Keeping the backslash produces the same literal through `glob_component`'s ordinary escape handling. Neither form changes slash anchoring. |
| 321:19, listing loop `<` to `<=` | 1 | The extra name is `.git/info/exclude`, which contains a slash and cannot be a directory-entry basename. |
| 325:51, `.git` comparison length 4 to 5 | 1 | For a four-byte name the extra byte is its terminating NUL. `cu_dir_next` uses `cu_str_cstr_copy`, which writes that NUL. Ignore-file text is not assumed NUL-terminated. |
| 327:12, default listing bit 0 to 1 | 1 | With a completed, stable listing, an existing `.rgignore` already set the bit. Otherwise the extra absent-file probe yields no rules when allocations succeed. This changes probe and allocation costs, not successful selection. |

The last row is a cost-only variant under successful, stable walks. It
is not exact resource, partial-listing, or filesystem-race equivalence.
No allocation-count budget or universal performance parity is claimed.
The retained guards remain in production. Mutation rejection is a
sensitivity measurement, not a correctness proof.

### Mutation selector safety

Closeout found that an absolute `MUTATION_FILE` could reach the runner,
whose path join could write outside its scratch copy. The wrapper now
accepts only the two documented relative source paths. Unset or empty
selectors retain the default pair. Other selectors return 2 before
invoking the runner.

`tests/mutation_selector_check.py` runs the wrapper with a temporary argv
recorder instead of the real runner. Against the pre-guard script it
reported 8 passed and 6 failed. Against the corrected script it reported
14 passed and 0 failed. All eight valid argv recordings matched exactly,
including the build and behavioral commands. No unsafe real mutation
was run. The check now runs in `tests/verify.sh`.

The reproduction, captured argv, and working-tree results are under
`build/ignore-handover.lr3RBB/selector-guard`. This guard changes no
mutation payload for the valid selectors used in the campaigns above.

### Earlier handover evidence, 2026-10-15

The handover reported `make -j8 all test`, 1000 ignore comparisons with
seed 7 at `-j1`, 1000 with seed 42 at `-j8`, 500 ordinary comparisons
with seed 7, and heresy-scan without findings. Those reports describe the
pre-fix source, separately from the executed follow-up checks above.

Ignore parsing has no dedicated arbitrary-byte fuzz harness. Existing
fuzz targets cover matching and stream framing, and Cerberus remains
bounded rather than a full ignore-walk semantics proof. Native Windows,
exotic ABIs, git's `core.excludesFile`, and universal ripgrep compatibility
remain outside this evidence. Malformed ignore patterns lack ripgrep's
parse diagnostics and can behave differently. Ancestor-relative spans
above 4095 bytes skip ancestor rules, as documented in README.
These verification runs did not commit, push, or install anything.

The final `git diff --check` was clean. Heresy-scan reported zero critical
findings and one warning on the existing README `Runtime TODOs` link.
That link was retained. A scoped comment audit removed nine comment lines
without changing executable statements. Local decisions and review limits
are in `build/ignore-handover.lr3RBB/decisions.tsv` and `review.md` there.
The available reader tool did not expose model overrides or reader model
identities, so a different-family review was not established.

## Unpublished 2026-10-14 review fixes verification

The [review fixes](docs/PERFORMANCE.md) passed full `make verify` under
`dcheck-tools exec` (the repaired private valgrind and Cerberus), receipt
`build/verify.MslDxS`, against the c-utils review fixes, since committed
unchanged as `2b1b488` and pinned in `ci/dependencies.json`. That
covers: the c-utils gate, the CLI fast check, framing and serial
sensitivity with Cerberus, TSan/ASan worker checks with the 3/3 race
control, all seven worker mutants, the cgrep deep tier, io_test, and
3000/3000 identical differential cases at `-j1` and `-j8` in each
profile. Coverage is 92.16%. Authority, regions and CBMC are skipped as
before; semantics is bounded.

Three earlier attempts failed. Each cause was fixed, not suppressed:

- Lint rejected a `{0}` initializer inside clang's own `mmintrin.h`,
  reached through `<emmintrin.h>`. The SSE2 code now uses GCC/Clang vector
  types and `__builtin_ia32_pmovmskb128`; the inner loops compile to the
  same `pcmpeqb`/`psubb`/`pmovmskb` instructions.
- MSan reported `putc_unlocked`, a glibc inline that reads `FILE` fields
  the uninstrumented libc initialized. One-byte parts now use
  `fwrite_unlocked`, at about 1.6 ms on heavy multi-file output.
- `fuzz_find.c` still called the `find` helpers moved into the tests; it
  now defines them and also checks one finder kept across every match.
- Valgrind's leak check saw four live worker threads because
  `test_parallel` exceeded dcheck's 30 s per-test limit at nice 15: the
  fixtures had grown with the 1 MiB slots (18.3 s versus 1.9 s at HEAD).
  The test now builds with 128 KiB slots, two input chunks each, and
  takes 2.8 s. Production keeps 1 MiB. The PATH valgrind (brew 3.27.1)
  still crashes on this CPU, as recorded below, so it is not the oracle.
- io_test's allocation-refusal case used a 12 MiB address-space limit,
  which the c-utils in-place growth now fits under. Refusal occurs from
  6 to 11 MiB serially and 8 to 11 MiB at `-j8`; the test uses 10 MiB.

The c-utils changes had an independent review that found two
use-after-frees and three arena accounting defects in the new realloc
growth path. All five were reproduced, fixed with regression tests that
failed first, and rerun clean under ASan. cgrep re-reads its stream
buffer after every reserve, so the new `CuVec` contract (growth may free
the old buffer) holds here.

Performance is still unqualified: the medians in PERFORMANCE.md come
from a busy workstation, not a quiet host. Nothing is installed.

## Unpublished batched writer verification

The batched writer passed full isolated `make verify`, receipt `verify.2MT7um`,
against committed c-utils `75748c7`. Required sanitizer/race/error/budget gates,
all 12,000 ordered comparisons, and bounded portable semantics passed. Coverage
is 91.59% cgrep / 91.78% dependency, with unchanged floors and optional exclusions.
[output-batch-followup.json](docs/output-batch-followup.json) retains the evidence.
The first attempt failed because its isolated snapshot omitted the CI workflow;
the actual workflow was copied and the full gate rerun, not disabled.

Native fixtures test exact fit, one spare byte, null/empty fragments without
publication, and a body longer than two channels. Portable fixtures check one
batch per line/warning, every fragment failure position, and exact partial
output. Four new body/failure/progress mutants join the existing ordering,
input-status, and cancellation mutants; all seven were rejected. ASan/UBSan/LSan
and TSan worker checks and the race detector's 3/3 negative control passed.
The local executable's `.text` matches the accepted build. Timed prototype
executables are frozen with exact report hashes; rebuilding the pre-comment
selected binary recovered its original whole-ELF identity.

The new internal callback borrows at most five fragments synchronously.
Descriptor/data pointers do not escape the callback. No new owned buffers,
coordinator states, flush/error boundaries, or 64 KiB channel limits changed.
Cerberus still does not interpret pthreads. Native tests and a read-only scout
review are not a concurrency proof. Performance remains unqualified, with no
installation or publication.

## Unpublished count and filename predecessor verification

The earlier candidate passed full `make verify` against committed c-utils
`75748c7`, excluding unrelated local dependency edits. All 12,000 ordered
comparisons and required race/error/budget checks passed; coverage is 91.44%
cgrep and 91.78% dependency. Receipt: `verify.y000FW` in the isolated acceptance
snapshot. [The durable record](docs/count-output-followup.json) retains JSON
receipts and logs. A separate [quiet Spot comparison](docs/spot-qualification.json)
now records 4,032 validated timed invocations with unchanged accepted C sources
and frozen binaries. It supports specific numbered/serial-output gains, not
universal nonregression or competitor parity. No new live Gitea run is claimed.

The native exact-size count oracle now covers 1024-byte boundaries and
asymmetric word groups. A duplicated fourth load is rejected natively and by
the existing <=300-byte semantic harness. All four previous serial mutants
still fail. Filename truncation and a missing filename colon add two output
mutants; all six fail the independent framing oracle. Literal path fixtures
include percent signs, colons, spaces, high bytes, and final unterminated lines.
Callback write positions, worker channels, cancellation and join rules remain
unchanged. Historical evidence below belongs to its named prior source/binary.

## Reproduce

Keep c-utils and dependable-c-agent-toolchain beside this checkout. Run
heavy checks sequentially under an outer memory cap when the devbox is
unavailable.

```sh
make verify
# Individual checks for targeted work:
make test difftest iotest
make check
python3 -I tests/difftest.py build/cgrep --cases 3000 --seed 7
python3 -I tests/difftest.py build/cgrep --cases 3000 --seed 42
python3 -I tests/benchmark.py build/cgrep --mib 64 --repeats 7
make mutate
```

For paired timings, add `--baseline /path/to/previous-cgrep`. The benchmark
checks output and status before timing and shuffles program order each
round. `make mutate` returns nonzero for surviving mutations, including
functional equivalents and tuning changes. Its regular-file limit does
not bound subprocess pipe captures, so the outer memory cap matters.

## Output, counting, and Gitea follow-up acceptance

Full acceptance finished in the pinned-Gitea-source pilot directory
`/tmp/cgrep-followup.lzy1oD/ci-pilot/cgrep-acceptance.DZNTxc`, with receipts
under `cgrep/build/verify.fhAgl4`. Cgrep coverage is 91.23% and dependency
coverage is 91.78%, above the unchanged 90% floors. All required compiler,
analyzer, sanitizer, MSan, release, wasm, Valgrind, bounded-semantics, fuzz,
framing, worker, and I/O-budget checks passed. The four differential runs
passed all 12,000 cases at `-j1` and `-j8`. Toolchain selftests also passed.
The measured executable SHA-256 is
`7ff504edb9f59d8381933a882c666f4df0f1c6a26afde9af4934d701569b0351`.

Terminal worker publication now includes residual output bytes. The
coordinator drops the pool mutex during output I/O while retaining ownership
of the published channel until acknowledgment or reassignment. Existing
native ordered-output, startup/read/close failure, cancellation, descriptor,
thread-join, TSan, and compiled worker-mutation sensors pass on this change.
The race control still fails 3/3 runs. This is executed native evidence,
not independent concurrency review or semantic interpretation of pthreads.

Decimal-prefix tests compare zero, powers-of-ten boundaries, and `ULONG_MAX`
with independent stdio formatting. Exact-size native word-count fixtures
cover byte values, alignment, tails, and density changes. A fourth compiled
serial mutant raises the lane batch bound enough to overflow the final byte
sum. The original mixed semantic fixture missed it. An all-LF case within
the existing 300-byte bound now rejects it natively and under Cerberus;
no bound, check, or coverage floor was relaxed. Numbering and LF framing
continue to pass the independent output oracle and large startup fuzz seeds.
After acceptance, an additional compiled missing-inline-LF mutant also
fails the framing oracle, covering the new combined body/LF write directly.

The CLI fixture checks that TTY output keeps its setup, non-TTY output
requests a 64 KiB buffer, and setup refusal returns status 2 before scanning.
Portable targets retain the existing setup. Native output-device errors,
pipe/file budgets, and stopping before later blocking inputs also pass.
Borrowed library streams are not reconfigured.

Broader benchmark checks reject wrong same-length bytes in both file and
pipe sinks, unterminated output, and LF/CR/VT framing errors. Each realistic
profile validates complete timed output. Seven-round paired plain/numbered
sweeps and the 21-round numbered remeasurement retain every raw sample.
`docs/performance-followup.json` embeds reports, source/binary identity,
validated JSON receipts, acceptance logs, and exact-output traces. Wall
results were contended; targeted cache advice does not prove cold physical
I/O. See [performance](docs/PERFORMANCE.md).

The Gitea source fetcher verifies complete pinned Git trees after safe
archive extraction. Tests reject tampering and unsafe paths, and confirm
that logged shell pipelines preserve failure. Before publication, the
trusted-push workflow had no server-triggered result. The local pilot reached full
acceptance PASS after its outer tool call timed out; the wrapper's terminal
exit status is not claimed. CI infrastructure uses a dedicated repository
runner, while the existing general runner remains unchanged. See
[CI setup](ci/README.md).

## Published ordered parallel-search acceptance

Opt-in `-j1` through `-j8` now passes full acceptance on Linux/glibc.
The backend uses private input windows and fixed 64 KiB channels, preserves
argv-order output/diagnostics, continues after input failures, cancels on
output failure, and joins every started worker. Default remains serial;
stdin/nonregular inputs and unsupported targets stay serial.

`make verify` passed in `build/verify.e3SwYZ`, with 90.77% cgrep coverage
and 91.76% dependency coverage, both above the unchanged 90% floor. Required
matrix/analyzer/ASan/UBSan/MSan/release/wasm/Valgrind/fuzz checks passed.
All 12,000 generated comparisons passed: seeds 7 and 42, 3000 cases each
at `-j1` and `-j8`, compared against ordered streaming ripgrep.

Native tests inject pool allocation, synchronization initialization, partial
thread creation, input-window allocation, open/read/close failures, and
nonregular races. They compare ordered output and read/close diagnostics,
count every created/joined thread, and check descriptors before/after each
scope. A raced FIFO plus a full later channel exercises cancellation wakeups.
TSan and ASan/UBSan/LSan run the required native backend. A compiled,
barrier-started race control is rejected 3/3 times; ordering, dropped-status,
and missing-cancellation mutants also compile and fail their behavioral
sensors. Missing native support/tools cannot silently pass those checks.

Eight workers scan seven 64 MiB regular files plus a later matching input
under a 32 MiB address-space cap. Parallel line-limit and allocation refusal
checks retain the later match; failed output returns status 2 and joins.
Nonregular fallback still avoids opening a later blocking FIFO after output
failure. These budgets are concrete bounded-line scenarios, not a total
memory bound independent of line length or worker count.

The initial stdio-cookie bridge caused an MSan diagnostic at an
uninstrumented libc buffer boundary and regressed heavy output. Direct
writer callbacks removed that boundary without suppression. Callback
framing, numbering, and each writer-failure position also pass wasm and
bounded Cerberus checks. Cerberus does not interpret pthreads; the fourth
semantic harness makes that exclusion explicit. Independent concurrency
review remains unavailable. Toolchain `make selftest` prints `selftest: PASS`,
including feature definitions before forced headers and continued rejection
of GNU `alloca` calls.

Raw logs are in `/tmp/cgrep-parallel-scope.laLQae`; durable performance
samples are in `docs/performance-parallel.json`. See
[performance](docs/PERFORMANCE.md) for the remaining single-file, small-match,
and output-heavy gaps. That implementation was subsequently published as
`5d8497a` and copied to Gitea. The follow-up above adds explicit mixed and
uneven content, pipe output, and file-cache advice. A server-triggered CI
run, physical cold-I/O proof, and traversal coverage remain unfinished.

## Historical serial optimization evidence

Uniform literals use a blocking-byte search rather than entering scalar KMP
after eight failed confirmations. A terminal mismatch skips a full window;
otherwise the rightmost mismatch blocks all candidate starts through it.
Discovery spans are at most the pattern length, and two failed full comparisons
advance at least one pattern length. This preserves O(input + pattern) work
without changing the immutable literal or persistent binary KMP state.

Line counting preserves `memchr` for sparse inputs and switches only after
four nearby LF bytes to a bounded 256-byte word batch. Copies establish safe
unaligned loads into declared unsigned-long objects. Checked value/object
widths enable only full 32/64-bit words with 8-bit bytes; other representations
retain character-only counting. The mask prevents cross-byte borrow errors,
and unsigned modular lane summation is intentional.

Scratch plain-unroll counters lost performance; an always-on word counter
helped dense input but nearly doubled sparse-input time. Those candidates
were rejected. The bounded adaptive counter retains sparse/mixed performance
in the measured cases. Final seven-repetition, randomized paired 64 MiB runs
against the frozen control show:

| Case | Before | Selected | GNU grep | rg -j8 |
| --- | ---: | ---: | ---: | ---: |
| Uniform, plain | 89.43 ms | 9.50 ms | 13.25 ms | 29.10 ms |
| Uniform, numbered | 102.39 ms | 17.44 ms | 21.60 ms | 30.68 ms |
| Medium absent, numbered | 20.34 ms | 17.74 ms | 20.94 ms | 5.37 ms |
| Medium late match, numbered | 19.96 ms | 17.69 ms | 21.17 ms | 5.47 ms |

Ordinary numbered scans improve about 11–13%. A 21-repeat remeasurement of
ordinary medium absent and small late-match cases gives essentially identical
before/after medians. Heavy matching output is roughly unchanged, not improved.
This meets the strongest-competitor target on the uniform stress case only;
parallel ripgrep remains ahead on ordinary multi-file and output-heavy cases.

The full `make verify` acceptance profile passes on the selected source.
Cgrep coverage is 94.08% above the unchanged 90% floor; dependency coverage
remains 91.76%. Compiler matrix, analyzer, ASan/UBSan, MSan, release, wasm,
bounded semantics, Valgrind, exact-output fuzzing, native budgets, and all
6000 differential comparisons pass. The first run stopped on an analyzer
diagnostic in the new zero-length allocation fixture; initializing the entire
one-byte empty allocation fixed that fixture without suppressing diagnostics.

New uniform tests cover blockers, offsets, lengths, NUL/high bytes, terminal
matches, and full-comparison work. Count tests use exact-size allocations,
unaligned offsets, partial tails, density changes, all byte values, and
adjacent LF/VT lanes. Compiled uniform-overstep, borrow-mask, and lane-sum
mutations fail natively and under Cerberus. The deep run completes 2,024,205
finder and 14,856 framing executions in its configured ten-second budgets.

Raw candidates, sensitivity logs, sparse/mixed probes, paired samples, and
acceptance are in `/tmp/cgrep-serial-prototypes.iigr0Q` and
`build/verify.okbYkH`. Final executable SHA-256 is
`d373ff111410fb77380d981de8b80b20c541efaec57449408f7c02e764eec04b`.
No commits or pushes occurred. The unrelated c-utils arena formatting and
concurrent edits to the older benchmark were preserved. Full-corpus semantics,
other representations/platforms, cold I/O, and universal performance parity
remain unverified. The subsequent parallel slice is recorded above.

## Durable-profile evidence

`make verify` now selects the source-pinned, hash-validated private checker
profile automatically. `dcheck-tools build` fetched the exact Cerberus commit,
applied the repair patch, built the engine and all private runtime objects,
verified the Valgrind archive, and rebuilt/installed it with baseline ISA
flags. It published the profile under `~/.local/opt/dcheck-tools/` only after
positive and negative probes passed. System Homebrew/opam packages were not
replaced. The second build reused and revalidated that profile.

Full acceptance passes with `DCHECK_CERBERUS`, `OPAM_SWITCH_PREFIX`, and
`VALGRIND_LIB` overrides removed, and with all temporary PATH entries removed.
Normal permanent prerequisites such as zig remain required. A deliberately
minimal PATH hid zig, and acceptance correctly rejected the required wasm
SKIP rather than reporting a weaker pass. Integrity tests reject stale
policy, changed binaries, extra files, external symlinks, changed pins, and
unusable launchers. The artifact hashes and tool probes are local integrity
and behavior evidence, not a signed security attestation or a proof of
checker correctness.

The installed profile is approximately 147 MiB, with a retained 545 MiB
build cache. Build and acceptance remain memory-capped, sequential, and
core-dump-free. Receipts are in `/tmp/cgrep-durable-tools.b6sLBY` and the new
`build/verify.*` acceptance directory. Temporary receipts are not dependencies
of the installed launchers. This remains a Linux x86_64 profile with pinned
direct opam dependencies and recorded system/transitive dependencies, not a
hermetic cross-platform distribution. No commits or pushes were performed.

## Acceptance coverage

`make verify` is the resource-capped acceptance entry point. It runs c-utils'
unit suite and gate, checks `cmd/main.c`, validates required dcheck receipts,
replays the exact-output framing oracle and compiled output mutants, runs
cgrep deep checks, rebuilds the executable, and runs native I/O budgets and
3000 differential cases each with seeds 7 and 42 at both `-j1` and `-j8`,
plus failure-sensitive native worker/race checks. Required checks cannot
silently disappear or become SKIPs. Authority, regions, absent CBMC proofs,
and optional tcc are explicitly outside this profile.

The framing fuzzer now checks exact bytes, path/number prefixes, first-NUL
warnings, match/error results, partial output, and stopping behavior. Its
reference scans the original input with naive matching and commits complete
lines at observed delivery boundaries. It does not call the production
finder, KMP, buffer, line-count, or formatting helpers. Known-output fixtures
calibrate the reference separately, including blank lines, unterminated tails,
NUL delivery suppression, earlier printed output, and failed reads.

Checked-in startup corpora contain four finder seeds and fourteen framing
seeds. Framing seeds reach 196622 bytes and include long-line growth, matching
across real refill boundaries, late NULs, and read errors. Per-harness sidecars
set input limits to 512 and 262144 bytes. dcheck copies seeds into writable
build corpora and logs the effective limits and seed counts; it never mutates
source seeds. The large-seed selftest is caught by fuzz alone.

The complete acceptance run passes with the explicit private repaired tools.
Both compiler/sanitizer/release/wasm gates pass. Dependency coverage is 91.76%
and cgrep coverage remains 93.09%, both above their unchanged 90% floors.
The 64 MiB regular-file and pipe checks pass under a 32 MiB address-space cap;
line limits, allocation recovery, output-device failure, and stopping before
a later blocking input pass. All 6000 ripgrep comparisons agree.

Native and ASan/UBSan/LSan framing replay passes all fourteen seeds and the
known-output fixtures. Three compiled output mutants are rejected by the
output oracle: removing LF, shifting line numbers, and shifting the NUL offset.
Toolchain selftest passes. The deep fuzz stage completes 2,409,908 finder and
9,446 framing executions in its configured ten-second budgets; the lower
framing throughput reflects larger inputs and a stronger oracle, not weaker
coverage requirements. A separate fifteen-second framing run also passes.

Receipts are under `build/verify.*` and `/tmp/cgrep-acceptance.cPILdm`.
Production sources and the original full native suites are unchanged. This
closes the identified acceptance/oracle/refill gaps, not all possible gaps.
Random fuzzing remains time-bounded and concrete, Cerberus remains scoped to
small harnesses, and the 8 MiB boundary stays in the native suite rather than
the framing fuzzer. General performance parity remains unproven.

## Earlier bounded-semantics evidence

`dcheck.conf` selects three harnesses in `semantics/`. The CLI harness runs
its existing suite unchanged. The finder enumerates haystacks and needles
up to two bytes over two byte values, plus targeted inputs up to 68 bytes.
The streaming harness uses payloads up to 24 bytes and short reads up to
seven bytes. Production chunk sizes and line limits are not overridden.
See [semantics/README.md](semantics/README.md) for the claim map and exclusions.

The full native test files and both fuzz sources are unchanged. `make test`
and dcheck's native gates also compile and run the new harnesses. dcheck
reports `dedicated harnesses: semantics (3 files); full tests retained in
native checks` in text and JSON instead of implying full-corpus semantics.

With the private launchers from `/tmp/cgrep-deep-tools.rrut4W`, the complete
`dcheck deep --json` run returned status `pass`, no failures, and explicit
skips for authority, regions, and absent CBMC proofs. The compiler matrix,
analyzer, ASan/UBSan, MSan, release, wasm, bounded semantics, full Valgrind,
and both fuzz targets passed. Line coverage remained 93.09%. Installed tools
were not replaced, so this receipt requires those explicit private tools.

Toolchain `make selftest` passes. Its seeded regressions verify dedicated
suite selection, default full-test selection, native-test retention,
empty-suite rejection, zero-exit diagnostic rejection, and JSON scope.
The bounded-suite sensitivity script checks baselines and rejects two
compiled mutants natively and under Cerberus: excluding a valid empty-search
start, and suppressing the scanner's read-error classification. The initial
CLI-only stream assertions missed the latter because close recovered the
error; a direct scanner-result assertion now rejects the same defect.
No production fix or original-test reduction was needed.

Receipts are in `/tmp/cgrep-bounded-semantics.kTAzwU`, including `deep.json`,
`sensitivity-final.log`, and `toolchain-selftest.log`. All heavy work remained
sequential under the 3 GiB/no-swap cap with core dumps disabled. This is
bounded concrete-execution evidence, not a formal correctness proof or
full-input-domain semantics verification.

## Earlier full-corpus tool attempt

Isolated builds and probes are in `/tmp/cgrep-deep-tools.rrut4W`. Installed
tools remain unchanged. The experimental Cerberus patch and build notes
are preserved in the sibling toolchain's `docs/checker-repairs/` directory.

- Valgrind 3.27.1 rebuilt with baseline x86-64 GCC flags passes the CLI,
  finder, and streaming suites with full leak checking and error exit 99.
  The 65536-byte `memset` probe passes, and a deliberate heap overrun exits 99.
- The private Cerberus build passes the complete CLI test, seven focused
  positive probes, and negative wrong-order and heap-overrun checks. Repairs
  cover mixed buffered output, `strerror`, `size_t` rank conversion, byte
  decoding, and redundant append-descriptor checks. Its virtual filesystem
  is seeded with `/tmp`; no product code or tests changed to hide failures.
- A complete deep attempt passed the compiler matrix, analyzer, ASan/UBSan,
  MSan, release, wasm, and coverage gates. Coverage remained 93.09%.
  Authority and regions stayed explicit skips. Cerberus's full finder suite
  then exceeded 300 seconds, and its streaming suite reached the scoped
  3 GiB OOM limit. The attempt ended before Valgrind, fuzz, or CBMC ran.
- Native `make test` and the three Valgrind runs passed separately after
  that interrupted attempt. They are not a completed deep receipt. No tests
  were reduced, failed diagnostics suppressed, or resource limits raised.

The private patch is not evidence of general checker correctness, other-ABI
parity, CHERI coverage, or independent review. Full semantics verification
still needs a larger environment or explicitly scoped bounded harnesses.

## Current mismatch-guided evidence

The matcher inspects the first failed comparison once, relative to that
candidate's start. A different expected byte at the mismatch becomes the
suggested alternate; otherwise the pattern-derived alternate remains.
The choice is local to the finder call, not stored in the shared Literal.
Only one switch is permitted, with at most sixteen false confirmations
before KMP. The extra diagnostic pass adds O(m) work once, so the O(n + m)
search bound and buffer ownership are unchanged.

Raw logs, the published control binary, paired benchmarks, and rerunnable
`sensitivity.py` are in `/tmp/cgrep-mismatch-guided.s6I0CZ`. Runs used a 3 GiB
memory cap, no additional swap, and reduced CPU priority.

- The compiler, analyzer, ASan/UBSan, MSan, release, and wasm gate passed.
  Coverage was 93.09%. Existing authority/regions skips and the documented
  deep-tool blockers remain unchanged.
- 400 default differential cases and 3000 cases each with seeds 7 and 42
  passed against the supported streaming reference. Unsigned-char tests
  and native memory-budget/I/O checks passed.
- Finder fuzzing completed 7,402,396 executions in 31 seconds, and framing
  fuzzing completed 133,592 executions in 31 seconds without oracle or
  sanitizer errors. Existing size caps were retained.
- Work-counter regressions failed on the published matcher: hot pattern
  bytes should not force a full KMP scan when the observed mismatched byte
  is absent. Fixtures include two/three hot values, nonzero search starts,
  a final positive match, and a mismatch against the active filter's value.
- The previous overlap fixture remains a correctness test. A new repeated-
  byte fixture deliberately forces both rejection budgets and an overlapping
  KMP resume with a nonzero suggested offset.
- Six defects compiled and failed assertions: ignoring the observed mismatch,
  diagnosing relative to the wrong origin, skipping starts during switching,
  skipping an overlapping fallback match, disabling bounded fallback, and
  subtracting the original anchor after switching. This is targeted
  sensitivity, not a new full mutation score.

Seven paired warm-cache repetitions on tmpfs, stdout discarded, randomized
program order. Median milliseconds for 64 MiB input:

| Corpus | Published control | Mismatch-guided | ripgrep auto-mmap |
| --- | ---: | ---: | ---: |
| Absent literal | 7.56 | 7.51 | 7.13 |
| Last-line match | 8.32 | 8.25 | 7.45 |
| Repeated-prefix failures | 8.91 | 8.77 | 7.48 |
| Original misleading byte | 8.55 | 8.53 | 8.06 |
| Uniform pattern | 90.05 | 89.02 | 107.23 |
| Two hot values | 81.81 | 8.68 | 28.70 |
| Three hot values | 71.16 | 9.47 | 21.39 |

The two/three-hot cases improved about 9.4x and 7.5x. Ordinary cases were
roughly unchanged. All seven measured cases fall within 2x ripgrep in this
run, but matched-output-heavy workloads, many small files, cold I/O, and
changing byte distributions have not been evaluated here. A bad suggested
filter can still enter KMP; the heuristic does not promise SIMD throughput
on every input.

## Previous adaptive-filter evidence

The matcher now prepares a distinct alternate candidate byte. Each filter
gets a constant rejection budget before switching once or falling back to
KMP. The pattern stays immutable across inputs. Candidate starts advance
by one after a failed confirmation; switching changes the offset used by
`memchr`, not the set of already rejected starts. At most sixteen full
confirmations precede the linear fallback, preserving O(n + m) search work.

Raw logs, the prior streaming binary, paired data, prototype, and rerunnable
`sensitivity.py` are in `/tmp/cgrep-anchor-switch.V9pXl3`. All work was local
under a 3 GiB memory cap, no additional swap, and reduced CPU priority.

- Native/wasm gate passed, including compiler matrix, analyzer, ASan/UBSan,
  MSan, release, and memory-budget I/O tests. Source coverage was 92.87%.
  Authority and regions remained explicit skips.
- 400 default comparisons and 3000 cases each with seeds 7 and 42 matched
  ripgrep 15.2.0's supported streaming behavior.
- Finder fuzzing completed 7,988,347 executions in 31 seconds with inputs
  capped at 512 bytes and no sanitizer/oracle failures.
- A byte-step sensor failed on the old matcher: the misleading-byte fixture
  should reject its suffix using the alternate filter without a KMP scan.
- Exact-size and overlapping fixtures cover candidate-base subtraction,
  transition between filters, and resumption into KMP after both budgets.
- Five isolated defects compiled and failed assertions: skipping the
  alternate, skipping starts during the switch, subtracting the wrong
  anchor, disabling bounded fallback, and skipping an overlapping match
  when entering KMP. These are scoped sensitivity checks, not a new full
  mutation score.

Seven paired warm-cache repetitions, randomized program order, stdout
discarded. Median milliseconds for 64 MiB input:

| Corpus | Before switching | Adaptive filters | ripgrep auto-mmap |
| --- | ---: | ---: | ---: |
| Absent literal | 8.10 | 7.95 | 7.79 |
| Last-line match | 8.46 | 8.12 | 8.17 |
| Repeated-prefix failures | 9.65 | 9.56 | 8.06 |
| Original misleading byte | 125.50 | 9.09 | 8.73 |
| Uniform pattern | 93.33 | 93.00 | 114.19 |
| Two hot candidate bytes | 84.41 | 84.26 | 29.50 |

The original bottleneck improved about 13.8x. Uniform patterns have no
useful alternate and retain the existing linear fallback. When both
chosen bytes occur frequently, the scalar fallback remains slower than
ripgrep. The new fixture records that limit rather than declaring general
performance parity. At 8 MiB it measured 11.55 ms versus ripgrep's 5.55 ms.

The historical streaming and matcher results below apply to their earlier
source snapshots, not the current adaptive implementation. No toolchain
installation, commits, or pushes occurred in this step.

## Previous streaming evidence

Raw logs, the prior whole-file binary, paired benchmarks, minimal tool
reproductions, and the rerunnable `sensitivity.py` script are in
`/tmp/cgrep-streaming.tgHPjo`. Runs used a 3 GiB memory cap, no additional
swap, and reduced CPU priority. The reference is ripgrep 15.2.0. Differential
comparisons use `--no-mmap`; performance comparisons retain automatic mmap.

| Claim | Evidence and observed result |
| --- | --- |
| Chunked platform input preserves its protocol | `tests/test_input.c` covers owned and borrowed streams, exact and partial reads, terminal caching, NULL/closed misuse, read-error precedence, errno zero, close failures, and first-error preservation |
| Refills preserve text and numbering | `tests/test_stream.c` covers short reads, matches around 64 KiB boundaries, CRLF and UTF-8 splits, no-match windows before numbered matches, and final unterminated tails |
| Binary handling retains the first offset and cross-chunk matches | Unit cases cover initial/late NUL, earlier printed lines, the three-byte initial probe, partial KMP state, multiple NULs, empty/one-byte patterns, and NUL at the line-cap probe |
| Long lines respect the exact limit | 8 MiB minus one and exactly 8 MiB work at EOF; exactly 8 MiB followed by LF works; one byte over reports the line limit; a following numbered line has the right number |
| Memory no longer scales with total file size | Native checks search 64 MiB text files and pipes under a 32 MiB address-space limit; a measured absent-literal run used 1564 KiB maximum RSS versus 67004 KiB for the prior whole-file binary |
| Failures preserve cleanup and continued search | Initial/growth allocation failures reset the failed buffer before later paths; read and close failures keep their first cause; earlier printed output remains after a later read failure |
| Output failure stops later inputs | A read-only output descriptor causes status 2 without opening a later blocking FIFO; `/dev/full` also returns status 2 |
| Behavior matches the supported streaming reference | 400 generated cases plus 3000 cases with each of seeds 7 and 42 passed, including larger text inputs around refill sizes |
| Supported compile/runtime targets remain green | Both project gates passed: compiler matrix, analyzer, ASan/UBSan, MSan, release, and wasm; source coverage was 91.76% in c-utils and 92.59% in cgrep |
| Byte matching and framing have independent sensors | Finder fuzzing completed 9,020,570 executions in 31 seconds; the memory-reader framing harness completed 53,262 executions in 31 seconds, including seeded long text and binary inputs |

Authority and regions were explicitly skipped. Seven isolated mutations
compiled and failed assertions: publishing bytes on read error, losing an
earlier errno zero, closing borrowed streams, dropping EOF tails, losing
numbering across windows, resetting binary prefix state, and accepting
byte-count overflow. The framing fuzz harness compares match existence
with a naive byte oracle and checks injected I/O errors. It does not replace
the unit output and line-number expectations.

### Streaming performance

Seven paired warm-cache repetitions on tmpfs, stdout discarded, randomized
program order. Times are median milliseconds for 64 MiB input:

| Corpus | Whole-file before | Streaming | ripgrep auto-mmap |
| --- | ---: | ---: | ---: |
| Absent literal | 32.58 | 7.83 | 7.92 |
| Last-line match | 32.95 | 9.34 | 8.36 |
| Repeated-prefix failures | 33.01 | 8.88 | 8.46 |
| Misleading candidate byte | 136.15 | 122.98 | 8.55 |
| Uniform pattern | 114.40 | 94.17 | 112.84 |

In this pre-adaptation snapshot, the first three cases were close to
ripgrep while the misleading-byte case was about 14.4x slower. That
measurement motivated the adaptive-filter step above. At 8 MiB, absent/last-line/repetitive searches measured about 2 ms.
These are local end-to-end timings, not a general ranking of search tools.

### Current limits and blocked checks

The line limit excludes LF and includes CR. Geometric growth retains under
16 MiB of window storage plus allocator metadata, with pattern and libc
storage additional. The 32 MiB process-budget tests use a small pattern;
there is no configurable total-memory budget. Counters reject input beyond
`ULONG_MAX` delivered bytes. Blocking stdio and buffered output do not
promise immediate interactive delivery or cancellation.

NUL checks occur before text search in each refill. Previously printed
lines remain visible after a later binary warning. Initial refill size is
three bytes to match the reference's encoding probe, but cgrep does not
strip BOMs or transcode. mmap binary policy, long binary-line refill
boundaries, Windows stream modes, and exotic ABIs remain outside the
compatibility evidence.

Valgrind 3.27.1 passes the new input-library test but crashes in its own
replacement `memset` during the framing test. `valgrind_probe.c` reproduces
this independently of cgrep: native 65536-byte fill passes, Valgrind's
8-byte fill passes, and its 65536-byte fill exits with SIGILL. Disassembly
of `vgpreload_memcheck-amd64-linux.so` shows EVEX `vpbroadcastq` at offset
`0x135df`, the reported illegal instruction. The crash's leak summary is
not a completed leak check. The installed checker was not patched and the
tests were not reduced to hide it.

At this earlier streaming checkpoint, Cerberus was blocked by its absent
virtual `/tmp` and incorrect mixed buffered `fwrite`/`fprintf` ordering.
A requested independent review could
not run because the delegated provider rejected its token/credit budget.
The receipts above are executed checks and main-thread review, not an
independent review or formal proof.

## Previous matcher-stage evidence

The results below describe the pre-streaming source, not current timings.
Raw logs, control source/binary, paired benchmark data, and a rerunnable
sensitivity script are in `/tmp/cgrep-literal-opt.M8p21h`. This is local,
temporary evidence. Runs used a 3 GiB cgroup memory cap, no additional
swap, and reduced CPU priority. The matcher source SHA-256 is
`ed1586b8abbc4756806394756ec226776a274affd3d4941c086368baaecc1a61`.

| Claim | Evidence and observed result |
| --- | --- |
| Literal output and status stay unchanged | Unit cases and 400 default differential cases passed; 3000/3000 cases passed with each of seeds 7 and 42 against ripgrep 15.2.0, `-F --no-config -j1` |
| Candidate offsets handle byte and buffer boundaries | `test_find.c` exhaustively checks short strings over NUL, `a`, byte 233, and byte 255 against a naive oracle, including all starts through one beyond the end; an exact-size heap fixture runs under ASan |
| Prefix fallback preserves overlapping and final matches | Both finder paths run against the naive oracle; targeted cases force fallback, overlap across its resume point, and a final legal match |
| False confirmations stay bounded | Adversarial long-prefix cases use a comparison counter, not wall-clock assertions; selective-byte fixtures also require zero full confirmations when that byte is absent |
| Line counting is optional | A newline-search counter observes no counting scans without numbering and nonzero counting scans with it; CLI tests independently verify output |
| Pattern allocation failure is reported and cleaned up | Injected allocation refusal checks literal initialization failure and CLI status 2; gate leak checks and targeted valgrind pass |
| C89 and exercised targets remain supported | Fast check and compiler matrix passed; analyzer, ASan/UBSan, MSan, optimized release, and wasm passed; source coverage was 98.17%; `-funsigned-char` tests also passed |
| Arbitrary-byte search stays within bounds and matches the oracle | ASan/UBSan libFuzzer run completed 10,273,273 executions in 31 seconds without errors, with inputs capped at 512 bytes |

Authority and regions were explicitly skipped because they are not
configured. Native I/O checks for allocation refusal followed by a
readable input, and `/dev/full`, passed. Targeted valgrind checks covered
the CLI tests and the strengthened finder tests with leak checking.

The fuzz run used:

```sh
clang -std=c99 -g -O1 -fsanitize=fuzzer,address,undefined \
  -fno-sanitize-recover=all -Iinclude -I../c-utils/include \
  -o /tmp/cgrep-literal-opt.M8p21h/fuzz_find \
  fuzz/fuzz_find.c src/*.c ../c-utils/src/*.c
/tmp/cgrep-literal-opt.M8p21h/fuzz_find \
  -max_len=512 -max_total_time=30 -rss_limit_mb=512 \
  -artifact_prefix=/tmp/cgrep-literal-opt.M8p21h/
```

The harness exercises the production finder code, not the CLI's file I/O.
It is a C99 driver around C89 sources. The regular gate separately checks
the C89 subset.

## Linear-work argument

Knuth-Morris-Pratt search, or KMP, uses a prefix table. Pattern preparation
builds it in O(m) time and space, where m is the pattern length. The
pattern-local byte histogram is a heuristic, not a claim about the input's
byte frequencies.

Each finder call scans candidates monotonically, permits only a constant
number of full confirmations, then falls back to KMP. The prefix table
makes that fallback linear in the remaining input.

The CLI rejects newline-containing patterns. A successful nonempty-pattern
call therefore consumes a distinct line containing at least m bytes. Its O(m)
confirmation cost can therefore be charged to that line. There is at most
one terminal unsuccessful call per complete-line window. Its O(m) cost
occurs only when at least m bytes are available, so it can be charged to
that window. Empty patterns simply advance through the lines.

Streaming checks NUL and the last LF only in new bytes. An unfinished tail
is not repeatedly searched or moved. After a complete prefix is consumed,
only a suffix of the new read remains to compact. Geometric growth copies
are amortized linear, and binary KMP retains state across refills.

Thus total search work is O(n + m), including preparation, rather than
O(n * m). The byte-search oracle, comparison
sensor, and fuzzing test plausible mistakes in this argument; they are not
a formal proof.

## Sensitivity and mutation triage

The scoped sensitivity script planted four defects in isolated copies.
Each compiled and then failed an assertion:

- Removing the bounded-confirmation fallback.
- Resuming after the anchor instead of after the rejected candidate.
- Discarding a valid overlapping prefix in KMP.
- Counting lines when numbering is absent.

The first full mutation campaign exposed verification gaps: byte 255 was
missing from the oracle alphabet, deeper prefix borders needed longer
patterns, and mutation commands lacked MSan for uninitialized-table
mutations. The strengthened checks cover those gaps in the final campaign.

The final command classified 176 mutations as 148 killed, 14 timed out,
10 surviving, and 4 invalid. Timeouts are bounded failures, not proof of
which defect occurred. All ten survivors preserve the tested functional
results and linear-work bound; some alter costs or heuristic choices:

| Surviving change | Reason |
| --- | --- |
| `sizeof frequency[0]` to `sizeof frequency[1]` | Same element type; neither expression reads the element |
| Initialize histogram counts to 1 rather than 0 | Uniform shift preserves ranking; successful prefix allocation bounds the pattern length below count overflow on exercised ABIs |
| Start anchor selection at offset 0 rather than 1 | Redundant consideration of the already selected first byte |
| Use `<` instead of `<=` for ranking | Selects a different equally frequent byte on ties |
| Bypass the single-byte fast branch | Generic confirmation at the matching byte returns the same result; no fallback-table access is needed |
| Start rejection counter at 1 | Enters the linear fallback earlier |
| Use `!= 8` instead of `== 8` | Enters the linear fallback on the first rejected candidate |
| Threshold 8 to 9 | Another constant confirmation budget, with the same results and asymptotic bound |
| Newline-count bound `<` to `<=` | Extra zero-byte search returns NULL without reading data |
| Initial errno 0 to 1 | Successful reads do not use it; tested native I/O failures set their own errno, and allocation failure uses the arena flag |

This is not a 100% mutation claim, nor a claim that all heuristic variants
have identical performance.

## Historical whole-file matcher performance

Seven warm-cache repetitions, one explicit file, stdout discarded, and
randomized program order produced these median milliseconds. These are
end-to-end process measurements on tmpfs, not isolated matcher timings.

| Corpus | 8 MiB before | 8 MiB after | ripgrep | 64 MiB before | 64 MiB after | ripgrep |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Absent literal | 4.56 | 5.01 | 2.55 | 31.47 | 29.94 | 7.92 |
| Last-line match | 5.96 | 4.81 | 2.37 | 41.98 | 32.80 | 7.85 |
| Repeated-prefix failures | 37.27 | 5.38 | 2.71 | 282.64 | 31.91 | 7.83 |
| Misleading candidate byte | 37.43 | 17.19 | 2.50 | 283.01 | 128.26 | 7.83 |
| Uniform pattern | 36.54 | 15.61 | 16.24 | 277.52 | 111.92 | 110.80 |

The repeated-prefix case improved 6.9x at 8 MiB and 8.9x at 64 MiB.
Ordinary absent-literal timings are roughly unchanged; these runs do not
establish a general improvement there. Whole-file allocation and copying
remain. The misleading-byte case demonstrates that linear fallback alone
does not achieve ripgrep's selective SIMD throughput.

## Remaining feature limits

Regex and terminal formatting are absent. Recursive traversal, ignore files,
and optional parallel search are implemented. Git's `core.excludesFile` is
not read. Detailed error wording differs from ripgrep. POSIX SIGPIPE can terminate the CLI
rather than returning output-error status 2. Native Windows and exotic
integer-width ABIs remain unverified.

Bounded semantics passes with the private tools, but full Cerberus
verification still exceeds the 300-second finder limit and 3 GiB streaming
memory cap. Installed tools retain their original failures. Earlier receipts are in
`/tmp/cgrep-deep-tools.rrut4W`; original reproductions are in
`/tmp/cgrep-deep.zE6slH`. No tests or gates were weakened. Earlier file-reader
and toolchain evidence remains in `/tmp/cgrep-resume-checks.bW2Hhj`.
