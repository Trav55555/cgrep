# Alternate Spot qualification also capacity-blocked

- User explicitly confirmed yes to another x86-64 Spot type under the same $0.20/hour bid and 45/60-minute deadlines. Requested eight-vCPU c6i, c6a, c7a and finally m6a pools in existing us-east-1 default subnets; all actual instance requests rejected InsufficientInstanceCapacity. Mixed-pool placement score9 was a forecast, not a capacity guarantee. No instance launched, no SSM job or qualification timings.
- One additional c6a attempt failed before instance launch because the temporary-directory suffix generated an invalid bucket name ending in a hyphen. Retain this implementation failure separately. Changed task-only bucket naming to a deterministic SHA256 hex suffix; later bucket creations succeeded. No benchmark or production C change.
- Stop after the promised final alternate attempt. Do not raise bid, launch on-demand, widen regions or retry without a new decision. Independently checked all five task attempts: no tagged instances or volumes, groups absent, all created buckets absent, timers inactive. Initial invalid-name bucket was never created.
- docs/batch-alternate-attempt.json retains sources, frozen payloads, logs, quotes, controls, exact read-only cleanup queries/timestamps/account/region and hash inventory at ignored build/batch-alternate-attempt-20261014. Original temporary evidence preserved. Local/native acceptance remains the completed output-batch slice; performance qualification still blocked. No installation, commit/push, runner/credential or live-CI changes.

# Blocked batched-candidate quiet qualification

- User agreed to proceed with quiet qualification before another optimization; fresh Spot approval follows the explicit request for it. Same one-host Spot-only bounds, bid <= $0.20/hour, 45-minute guest deadline and prearmed 60-minute task-cleanup fallback. No on-demand, install, publish, runner or C-change authority.
- Prepared frozen accepted batched 0e7c1cf2, previous counting 0042aeb9 and installed 547021b9 binaries, GNU and rg. Six workloads, plain/numbered, all 21 fixed configurations, 21 randomized rounds = 5,292 planned exact-output invocations. Local capped smoke passed 42 timed invocations; benchmark11/quiet6 tests passed. No C acceptance rerun or candidate/source edit.
- Prelaunch review found real cloud harness gaps. Repaired timeout/partial-output preservation, final-upload status, bounded transfer/group shutdown, idempotent failure-isolated task cleanup, atomic state and emergency identity recovery. Prearm timer before any cloud allocation. Generated scripts parse; real timeout/corruption/final-upload controls and SDK-fake cleanup/state controls pass. No guest package installation. Reviewer model unavailable, so no cross-family independence claim.
- Spot launch rejected InsufficientInstanceCapacity in us-east-1d ($0.1764 quote), us-east-1b ($0.1824) and us-east-1a ($0.1976), same c7i.2xlarge and $0.20 max bid. No instance launched, no SSM jobs, no qualification timings. Stop rather than raise bid, use on-demand, switch type or retry indefinitely.
- All three task buckets/security groups independently verified absent, tagged-instance queries empty, fallback timers inactive. Retain failure logs/states/controls/payloads at ignored build/batch-spot-attempt-20261014; docs/batch-spot-attempt.json records evidence and hash inventory. Original /tmp/cgrep-batch-spot.hluQhjaA, /tmp/cgrep-batch-spot2.yefuf71i and /tmp/cgrep-batch-spot3.hl5s_uri preserved. Candidate stays performance-unqualified. Next decision: another time window or explicit alternate Spot type approval.

# Completed bounded output-callback experiment

- User said "proceed" after the recommendation to test fewer callbacks/copies before any concurrency-library extraction or handoff redesign. Local implementation/verification only; no renewed cloud, install, commit/push, runner, or live-CI grant.
- Evidence /tmp/cgrep-output-batch.jjaoxzt9. Frozen baseline is the accepted count/filename candidate 0042aeb9, not the installed 547021b9 build. Exported committed c-utils 75748c7 excludes the unrelated arena edit. Production C and tests were frozen before experiments; unrelated benchmark files preserved.
- Compared disposable vector-only, fitting-record fast path, and fast path plus one-byte stores under sequential 3 GiB/no-swap/nice15/no-core scopes. 448 timed exact-output diagnostic invocations and 16 exact-output Callgrind profiles retained. Busy workstation results are not quiet qualification. Vector-only barely helped; selected bounded variant reduces worker heavy-output instructions about 5.3% plain / 7.2% numbered in the last comparison. This is work reduction, not native speedup.
- Selected changes only: borrowed fragment vectors consumed synchronously; preflight checks len <= room-total before adding; fitting records copied before one used update; one-byte fragments direct-stored; unchanged splitting/publication fallback. Fixed channels/storage, coordinator state/locks/flushes/joins, matching, FILE output, and cancellation rules unchanged. No descriptor/pointer escapes the callback.
- Added exact-fit/one-spare/null-empty/no-eager-publication native tests, a body larger than two channels, one-batch framing and every-fragment failure/partial-output checks. Existing worker ASan/UBSan/LSan and TSan/race-negative controls passed on the selected prototype. All seven compiled worker/output mutants rejected, including four new callback/progress defects. Read-only scout review found no vector migration hazard and identified fast-path boundary obligations, now tested; this is not pthread semantic proof.
- Applied only src/cgrep.c, src/parallel.inc, tests/test_parallel.c, tests/parallel_check.py from the selected prototype, preserving earlier changes. Full isolated make verify exited 0, receipt verify.2MT7um: 12,000 ordered comparisons, required sanitizer/race/error/budget gates, matrix/analyzer/release/wasm/Valgrind/fuzz and bounded semantics; coverage 91.59% / 91.78%, unchanged floors/optional exclusions. Initial snapshot lacked .gitea workflow, failed CI-contract tests before deep acceptance; copied actual workflow and reran all gates, retaining the failure.
- Local build/cgrep rebuilt against the same committed-dependency snapshot; io_test passed and .text equals the accepted build. Installed cgrep and unrelated benchmark files unchanged. Exact timed byte binary recovered with original source/flags/cwd before the explanatory comment; its whole SHA matches the report, then accepted source/binary restored. All baseline/vector/fast/byte timed binaries frozen.
- Durable docs/output-batch-followup.json records receipts, all diagnostic raw wall/CPU samples, identities, failed preparation, claims and limits. Raw Callgrind/source snapshots/binaries/scripts retained at ignored build/output-batch-20261014; recorder reruns from that directory. Final recorder checks raw Callgrind summaries and finite timings, and rejects five controls: failed acceptance, wrong timed identity, missing raw sample, NaN sample, and altered instruction total. Original report bytes restored; no benchmark/full-C rerun. Final helper, controls, logs and hashed inventory retained with raw artifacts. A final read-only scout review found no actionable issue but did not independently rerun tests or prove concurrency semantics.
- This local slice is correctness-verified and performance-unqualified. No new cloud resources, library, handoff pipeline, install, commit/push, runner/credential changes or live CI. Further quiet paired qualification and any ordered-handoff changes remain separate next steps; instruction savings do not resolve the earlier wall-time gap.

# Completed Spot qualification and selected output profile

- Approved scope completed on one c7i.2xlarge Spot instance i-0074a9309ac671d15, launched 2026-10-14T06:25:12Z and terminated by 06:45:06Z. No on-demand fallback. Max bid $0.20/hour; quoted compute estimate $0.0578, estimated total below $0.10, not a verified AWS bill. Guest 45-minute deadline and independent local 60-minute termination timer were installed before benchmarking; timer stopped after task cleanup.
- Final SSM wrapper exited 0. All 4,032 invocations, 12 case/output-mode combinations, 21 rounds and all 16 fixed configurations validated full output/status/stderr after wall timing. Own CPU subtracted; admission/load rules unchanged. During final pair, background CPU <=0.294%, aggregate I/O wait <=0.125%; dedicated 3 GiB/no-swap/nice15/no-core scope peak 420,192,256 bytes before profiling.
- Preserve preliminary attempts: first completed 4,032 comparisons but failed I/O eligibility due per-sample EBS fsync; second stayed quiet but stopped after 3,680 samples on concurrent monitor/archive changes. Tmpfs bookkeeping and stable archive snapshots fixed measurement errors without weakening gates or suppressing diagnostics. Third full run qualified. No samples dropped and no C changes between runs.
- Serial dense numbering improves about 11%; serial heavy output about 28% plain / 21% numbered. Medium plain late-match -j8 regresses 3.01 to 3.27 ms, CPU 14.54 to 15.62 ms. No universal nonregression, installation, or strongest-competitor parity claim.
- Largest fixed/equal-worker remaining gap: heavy plain -j8 candidate 36.69 ms versus rg -j8 14.81 ms. Exact-output traces: candidate 1,522 futex / 192 write calls versus rg 32 / 16. Instrumented durations are not native time. Both perf captures validated 100 full outputs. Native perf hardware probe allowed counters; user-space cpu-clock sampling resolves search_text about 20%, channel_write about 17%.
- Remote perf report failed SIGPIPE after addr2line configuration; local perf 6.17.13 refused newer ABI. Kept both failures. Private Ubuntu perf 7.0/dependencies extracted from signed/hash-verified packages decoded unchanged data with --no-inline and frozen ELF symbols, exit 0. Libc remains unresolved; no kernel/blocked-time or instruction-address attribution. Profiling was not independently quiet-qualified.
- Evidence docs/spot-qualification.json embeds all timed journals/reports including failures, summaries, profile diagnostics, identities, nine rejecting evidence controls, and cleanup/cost records. Raw archives/perf.data/full trace output/frozen binaries/scripts retained at ignored build/spot-qualification-20261014, with exact inventory. Original /tmp/cgrep-spot.jEZZEXQs remains intact. Revalidation ran successfully from stable HOME artifacts.
- Cleanup verified: task instance terminated, vol-0be5de3aba48b58ee deleted, Spot request sir-kj9fpjwj closed, task bucket/security group absent. Existing SSM role and other resources unchanged. No install, commit/push, live CI, runner/credential changes, uniform-pattern workload, new implementation, or repeated C acceptance.
- This approved experiment is complete. Next work would investigate heavy-output callback/copy and ordered-channel handoff costs, plus the ordinary -j8 regression, with error/flush boundaries and 64 KiB limits intact. That is a recommendation, not an implementation grant.

# Temporary Spot qualification host

- User approved temporary idle x86-64 provisioning with "yes" and specified "spot". Local provisioning/profile/retrieval/cleanup only, still no code promotion, install on workstation, commit/push or live CI.
- Plan at 2026-10-14T06:04Z: one c7i.2xlarge, us-east-1d, Ubuntu 24.04 Canonical ami-0fa5967347d08d2df, 8 vCPUs/16 GiB host. Spot one-time/terminate interruption, max price $0.20/hour, no on-demand fallback. Dedicated task SG has no ingress; existing SSM profile reused unchanged; IMDSv2 required; encrypted 20 GiB gp3 delete on termination.
- Max runtime 45 minutes via guest deadline, independent local cleanup fallback at 60 minutes; conservative one-hour max estimate <$0.22 including EBS/public IPv4 and small object transfers. Parent records costs and terminates only task-created resources. No existing instance/role/SG/bucket edits. Temporary private encrypted transfer bucket removed after retrieval.
- Scope: unchanged installed 547021b9 vs candidate 0042aeb9 plus unchanged GNU grep 3.11/ripgrep 15.2.0. Six numbering/heavy-output/tiny-file workloads, no uniform case; fixed threads and all timed bytes/status/stderr validated. Benchmarks and installs/profiling sequential under user 3 GiB/no-swap/nice 15/no-core scopes. Profile largest fixed/equivalent-config remaining gap only after measured eligibility.
- Limits: VM CPU/libc/kernel differ from workstation; quiet admission is measured not dedicated-hardware/frequency proof. Spot interruption retains partial outputs, never becomes PASS. Any incompatibility is recorded rather than silently rebuilding either candidate or changing flags.
- Evidence /tmp/cgrep-spot.jEZZEXQs; all cloud IDs, rates, timing/counter reports, source/binary hashes and cleanup state retained without credentials or signed URLs.

# Focused installed/candidate qualification attempt

- User explicitly reprioritized numbering, heavy output and small-file costs, then said "proceed" twice. No uniform-pattern work or new counting/worker/channel optimization before a quiet paired comparison.
- Approved local experiment at 2026-10-14T05:44Z; main at a5ab2e769369ce955cbc90fb74e31e895daf6b27. Production C sources and build/cgrep unchanged from acceptance. Installed 547021b9 versus current candidate 0042aeb9 frozen without installing anything.
- Evidence /tmp/cgrep-paired-quiet.62TlECwc. One bounded 120-second admission on the existing workstation; thresholds unchanged: five seconds, background <=10%, I/O wait <=2%, admission load/logical CPUs <=0.5. No uncapped/noisy fallback or stopping unrelated processes.
- If admitted: 21 randomized paired rounds over six numbering/output/tiny-file cases, plain and numbered separately, GNU grep and all fixed cgrep/rg -j1/-j2/-j4/-j8/default configurations, complete output/status/stderr validation. Retain noneligible samples. No uniform workload in the selection.
- Profile only after eligibility: retry perf, record denial if unavailable; validated-output strace -c for writes/synchronization, never interpret traced durations as native time. Choose a remaining gap against fixed/equivalent worker configurations, not best-thread cherry-picking.
- Authority remains local checks/profiles, no new implementation, install, commit/push, credential/runner changes or live CI. Existing artifact work complete locally but live persistence still separate.
- Attempt result: exit 2 after 120 samples; background CPU min/median/max 13.08/32.19/76.09%, every admission interval above the 10% limit. No timings, profiling, uniform work or new optimization started. Installed/candidate source paths still match frozen identities. Durable evidence docs/paired-quiet-attempt.json; prepared six-case recipe in evidence compare.sh. Qualification remains blocked on an idle host/window, not on missing implementation. No further current-host retries without a materially changed quietness condition.

# Active performance qualification and artifact completion

- Approved by user "proceed" at 2026-10-14T05:18Z, following the next-local-slice plan. Local qualification and artifact work only; no installation, commit, push, credential/runner changes or worker redesign.
- Snapshot: main, HEAD a5ab2e769369ce955cbc90fb74e31e895daf6b27. Count/filename sources and build/cgrep match the durable acceptance record with no tested-source drift. Existing CI/quiet-monitor edits remain unpublished; preserve unrelated tests/benchmark.py, tests/test_benchmark.py and sibling arena edits.
- Direction: docs/PERFORMANCE.md b4a9f4e345dcbfc4e82fad70397c07672473453901cf717efea5a3c73b50be98; global practice ccfd3151a61e0b2c9e1911a6da5a9c4c4693b48117b7993044423b827fd5d6f3.
- Evidence: /tmp/cgrep-qualification.qMveIS9Y. One short quiet-admission attempt with unchanged thresholds; if admitted, fixed-config four-way plain/numbered profiles with all output validated. Retain noneligible results, no fallback timing campaign or stopping unrelated work. All heavy work sequential under 3 GiB/no swap/nice 15/no cores.
- Artifact acceptance: collect latest real receipts and logs; verify allow-list/hashes/status; test fresh local retrieval after disposable job cleanup and failure retention. Any prior selftest log used must retain its source/hash and be labelled composite local pilot, not a fresh CI job. Live server persistence remains blocked on separate authorization.
- No repeat of completed full C acceptance unless tested C changes. New artifact checks receive narrow tests and shell/workflow consistency checks.

## Qualification/artifact local result

- One 30-second unchanged-threshold admission failed: background CPU min/median/max 7.70/16.20/35.95%, no five-second admission, no benchmark timings started. No repeated retry or contended fallback.
- Added ci/check_artifacts.py for downloaded-directory inventory, size/hash, schema/status and symlink checks. --receipts additionally applies existing required dependency/CLI/deep receipt policy and refuses failed acceptance. Integrity is not authenticity or proof of a manifest's claims.
- Collected latest actual count/filename receipts through the production EXIT trap. Composite pilot uses retained prior selftest from unchanged clean b907b6a toolchain; explicitly not fresh wrapper execution. Revision null avoids attributing uncommitted tested sources to published a5ab2e7.
- Success and seeded-exit-7 bundles survived local ZIP retrieval after disposable job cleanup. Received receipt hashes match actual source receipts. Failure cannot be promoted by --receipts; successful retrieval passes all required receipt checks. Original evidence preserved; live server protocol/retention unverified.
- Artifact suite 13 tests, CI contract 7, quiet monitor 6 passed. Seeded unchecked-hash mutant fails same-length-tamper sensor for the expected ValueError-not-raised reason. Original failure 7 survives collector failure; failed collection cannot turn success into PASS. Shell syntax/diff checks passed; no tracked ignored build outputs.
- Durable evidence docs/qualification-artifacts.json includes logs, raw admission samples, actual composite bundle, hashes, scripts and limitations. Production/native-semantic C source hashes and build/cgrep unchanged; no repeat of completed C acceptance. Unrelated benchmark edits verified unchanged; no sibling source edits.
- Local artifact verification complete, overall slice partially verified. Remaining inputs: idle x86-64 host/window for timing qualification; separate publication/live-run authorization for Gitea persistence. No installation, commit/push, credential/runner change, external service call or adjacent optimization.

# Active count and filename-formatting slice

## Approval after investigation

- User: "proceed", after the investigation recommended LF counting and filename formatting.
- Approved at: 2026-10-14T04:04Z; branch main; HEAD a5ab2e769369ce955cbc90fb74e31e895daf6b27.
- Scope: test count grouping/batch size and avoid per-line printf filename formatting, as separately measured changes. Preserve serial default, 64 KiB worker channels, bounded storage, ordering, input-error continuation, raced-FIFO safety, cancellation and joins.
- Authority: local edits, profiles and verification only; no renewed commit/push/install grant.
- Native benchmark baseline is installed 547021b9, whose .text equals accepted baseline 7ff504ed. Freeze it before edits. Dependencies for checks use committed c-utils 75748c7, excluding the user's arena formatting.
- Existing CI/quiet-monitor work remains; preserve modified tests/benchmark.py and untracked tests/test_benchmark.py. No adjacent worker redesign or mmap addition.
- Acceptance: exact output/error oracles, expanded count boundary tests, original native/Cerberus mutants plus targeted new defects, capped full make verify, paired fixed-config full-output profiles. No stable speedup or promoted timing winner without quiet-eligible evidence.
- Risk: memcpy bounds/representation and byte-lane arithmetic for count groups; filename bytes/length and sticky output errors for FILE writes. No new owned storage or shared mutable state.
- Evidence: /tmp/cgrep-count-format.wMMo2q; all heavy work sequential 3 GiB/no swap/nice 15/core dumps off.

## Count/filename local result

- Implemented four-word independent copied loads and a 1024-byte adaptive span, still reducing at most 16 words and preserving sparse memchr and representation fallback. No new storage/shared state.
- Replaced per-line FILE filename printf with cached-length fwrite and a colon fputc. Callback path, buffering, ordering and errors unchanged.
- Added exact-size 1024-boundary and asymmetric-word oracles, literal percent/colon/space/high-byte filename fixtures, a duplicated-word native/Cerberus mutant, and truncated-name/missing-colon framing mutants. All five serial mutants and six output mutants rejected.
- Strengthened benchmark_parallel to validate every timed byte after the timer stops and retain raw CPU samples. A same-length-corruption sensor rejects the old gap.
- Full isolated pinned-source make verify exited 0; receipts acceptance/cgrep/build/verify.y000FW in /tmp/cgrep-count-format.wMMo2q. 12,000 comparisons, race/error/budget controls, required checkers passed; cgrep coverage 91.44%, c-utils 91.78%, unchanged floors/optional exclusions.
- Quiet admission failed after 180 seconds; no timing winner is promoted. Diagnostic four-way fixed-worker sweeps and a 21-round serial recheck retained. Simpler batch-only control did not provide the grouped counter's dense gain. Sparse/cluster CPU roughly unchanged in this run. Full-search output cost and numbered dense CPU look lower, but are not quiet-host claims.
- Exact-output Callgrind totals fell 138591496 to 122474626 instructions for dense numbered absence, and 431770419 to 321617400 for heavy numbered output. These are instruction-work reductions of 11.6% / 25.5%, not CPU-time shares or stable speedups.
- Durable receipts/logs/reports/hashes in docs/count-output-followup.json; regeneration/sensitivity via tests/record_count_output.py. Tested sources match live C/test/tooling hashes. Unrelated benchmark and sibling arena work preserved.
- Code correctness verified; performance qualification partial and target remains unmet. No install, commit, push or new live CI. Remaining step: quiet paired qualification before promotion/publication.

# Earlier CI artifacts and single-file numbering slice

## New approval snapshot

- Approved by user: "proceed", following the recommended items 1–3.
- Branch: main; HEAD: a5ab2e769369ce955cbc90fb74e31e895daf6b27.
- Preserve modified tests/benchmark.py, untracked tests/test_benchmark.py, sibling c-utils/src/cu_arena.c, and prior evidence.
- Direction: docs/PERFORMANCE.md strongest-valid-competitor target; ~/.agents/programming-north-star.md small correct verifiable slices.
- Scope: persist allow-listed CI logs/JSON receipts before runner cleanup; measure and require low background interference for an accepted baseline; profile/prototype single-file numbering and promote only evidenced improvements.
- Acceptance: artifact collection on pass and failure without credentials; pinned uploader with missing-file failures; sensitive collector tests; all timing samples retained with host-interference metadata; exact output and C89 byte-oracle/mutation/full acceptance for any production C change.
- Non-goals: traversal, regex, automatic worker selection, small-file/output optimization beyond numbering, runner credential changes, stopping unrelated processes, or weakening any gate.
- Authority: local implementation/checks only. This "proceed" does not renew commit/push permission. Live upload integration remains unverified until authorized publication.
- Heavy work: sequential scoped MemoryMax=3G, MemorySwapMax=0, nice 15, core dumps disabled; no uncapped fallback.
- Evidence: /tmp/cgrep-numbering-ci.9zCgJw. Read-only reviewer rejected again with provider 402; no independent review.

## Local receipt and current blocker

- CI collection writes an allow-listed hashed bundle in an EXIT trap, preserves check failures, and fails missing successful evidence. The workflow pins upload-artifact v3.1.3/a8a3f3a, always attempts upload, rejects missing files, and requests 30 days. node24/v4 was deliberately not assumed compatible with act_runner v0.2.11.
- Local artifact/CI/shell sensors passed; collected the original accepted pilot's real logs/JSON and validated its deep receipt. Live server upload and a fresh full acceptance for this slice remain unverified. No commit/push.
- Added scope-aware CPU/I/O quietness admission. Load is checked only before timing, because benchmark workers themselves contribute to host load. The monitor-path regression rejects the seeded old during-run load gate.
- Original 120-second attempt: background CPU 16.90/31.40/98.24% minimum/median/maximum, no admission. Fresh 10-second recheck: 7.26/9.83/18.35%, no five consecutive eligible seconds. Final 120-second attempt: 7.13/30.60/89.55%, no admission. No benchmark timing started and all observations are retained.
- Five isolated strict-C89 variants passed existing native count byte-oracle fixtures. Seven-round contended CPU probes: dense baseline median 18.50 ms, quad1024 13.74 ms per 150 MiB counting; sparse/cluster/random/absent probes also retained. This is not full-search or quiet-host evidence. No production source was changed or candidate promoted.
- Durable evidence: docs/numbering-prototypes.json and docs/QUIET-BENCHMARKS.md. Scratch recipe/sources: /tmp/cgrep-numbering-ci.9zCgJw/prototype_count.py and prototypes/. Required next input is an idle host/window; do not kill unrelated work, relax thresholds, or claim timing wins from contended probes.
- Direction hashes refreshed: programming-north-star ccfd3151a61e0b2c9e1911a6da5a9c4c4693b48117b7993044423b827fd5d6f3; docs/PERFORMANCE.md aac1fa073a3e7af5380467d4988450dee21f2437311863871d8598255b15afb7. The historical direction hash below belongs to the completed prior slice.

# Completed performance and Gitea CI follow-up

## Current-state alignment

- Goal: meet the strongest valid GNU grep/ripgrep competitor without weakening output order, failure handling, memory bounds, or checks.
- Project direction: docs/PERFORMANCE.md, SHA-256 ed1a25df5f8b074a3eabdf06a0818c49967434581113d9dc109c8f9ecedf5812.
- Baseline: verified cgrep 5d8497a, toolchain b907b6a. Ordered medium-file scans win; heavy output, small matching files, and single-file numbering still lag.
- Gitea: existing local act_runner service is active. Existing hosted ARM runners cannot substitute for the required x86-64 checker profile without new evidence.
- Preserve tests/benchmark.py, tests/test_benchmark.py, and sibling c-utils/src/cu_arena.c edits.
- Issues: prior read-only GitHub issue inventory returned no open items; local TODO/performance documents define this work.

## Approval snapshot

- Branch: main
- HEAD: 5d8497abebd3339b01c08f0e551fcc0c955f87d1
- Status: modified tests/benchmark.py; untracked tests/test_benchmark.py; sibling arena formatting modified.
- Approved at: 2026-10-13T21:46:43Z
- Approved by: user, "use gitea setup for ci, proceed with all of the above"
- Authority: implement all five proposed slices and Gitea CI setup. Do not infer a fresh general commit/push/deploy grant from the completed previous publication phase. Report publication required for live CI separately.

## Contract and sequence

1. Profile output formatting/transfers/flushes; keep only bounded batching changes that pass independent output/error tests and paired full-output benchmarks.
2. Use existing Gitea setup for CI. Require the same full acceptance profile on a suitable runner with memory/swap limits, pinned sibling source, and no missing-check success. No deployment jobs or untrusted PR execution on the personal host.
3. Reduce small-file scheduling/handoff overhead only with exact ordered-output and startup/input/output failure sensors.
4. Probe safe C89 newline-count alternatives; retain sparse-input safeguards and byte-oracle/mutation coverage. No SIMD/extension-dependent production path or diagnostic suppression.
5. Add reproducible uneven and real-repository explicit-file corpora, file/pipe sinks, and targeted file-cache advice. Label cache/filesystem limits honestly; never drop global caches or imply directory traversal support.

## Delivered evidence and remaining publication

- Implemented combined terminal channel/result publication and unlocked coordinator output I/O, direct decimal prefixes/LF output, non-TTY CLI buffering, and 16-word bounded lane reduction. Serial default and storage limits are unchanged.
- Full pinned-source acceptance reached PASS in `/tmp/cgrep-followup.lzy1oD/ci-pilot/cgrep-acceptance.DZNTxc`; receipts `cgrep/build/verify.fhAgl4`. Coverage is 91.23% cgrep, 91.78% c-utils; all 12,000 comparisons and required race/error/mutation/budget checks pass. Toolchain selftests pass. The outer caller timed out, so no CI wrapper exit-status claim.
- Added mixed/uneven/repository benchmarks with exact timed file/pipe validation and targeted cache advice. Seven-round paired profiles plus a 21-round regression remeasurement are retained in `docs/performance-followup.json`. Contention prevents stable-host guarantees. The strongest-competitor target remains unmet.
- Created private Gitea mirrors of already-published heads and dedicated repository runner 441. Service `act_runner-cgrep.service` is active with 3 GiB/no swap/nice 15. Existing Docker runner unchanged. No new source/workflow commit or push occurred.
- HTTPS archives replace timed-out direct SSH clones. Complete pinned Git trees are checked before executing dependencies; no tea credential is copied into artifacts.
- User authorized commit/push with "yes". Published only this slice to Gitea main as `a5ab2e769369ce955cbc90fb74e31e895daf6b27`. GitHub was not pushed. Unrelated benchmark edits and sibling arena formatting remain excluded.
- Server-triggered Gitea run 330, job 773, runner 441 completed successfully. Retrieved server logs contain toolchain selftest PASS, CI acceptance PASS, all 12,000 comparisons, race control 3/3, four framing mutants, and coverage 91.23% / 91.78%. URL: https://git.traversannan.com/trav/cgrep/actions/runs/330.
- Live acceptance scope enforced MemoryMax=3221225472 and MemorySwapMax=0; observed peak 890163200 bytes. Proof: `/tmp/cgrep-followup.lzy1oD/gitea-ci-receipt.json`, server logs `gitea-job-773-logs`.
- Runner cleaned the temporary job directory after completion. Use retrieved server job logs for this result, not the deleted scratch paths; the runbook's scratch-directory retention wording needs clarification before promising post-run JSON artifact availability.
- An all-LF case within the existing 300-byte semantics bound was needed to reject the new lane-overflow mutant. Original mixed-byte semantics alone did not reject it.
- Targeted CLI/CI/benchmark/native checks were rerun after the bounded-fetch and log-streaming refinements. Production C source matches the accepted binary's source hashes.

## Verification and boundaries

- Heavy checks sequential under MemoryMax=3G, MemorySwapMax=0, nice 15, core dumps off.
- Default remains serial; automatic worker selection is out of scope.
- Narrow tests first, then make verify and toolchain selftest where affected. No coverage-floor reductions or required SKIPs.
- Benchmark current and saved baseline in randomized paired rounds with complete output validation. Keep all raw samples and source/binary hashes.
- Evidence/decision trail: /tmp/cgrep-followup.lzy1oD/decisions.tsv.
- Independent subagent provider rejected both scouting calls with HTTP 402; no independent review obtained. perf counters are unavailable under current permissions; use deterministic counters/microbenchmarks and strace summaries instead of changing kernel policy.
