# Evidence index

Use this page to identify which source/executable a claim describes. Start
with [current performance status](PERFORMANCE.md) or
[correctness verification](../verification.md); load a raw report only when
checking its samples, receipts, identities or failure history.

The JSON reports remain at their original paths, unchanged by this cleanup.
Their status fields describe the time of capture. Later evidence may qualify
the same executable without rewriting an earlier failed attempt.

## Executable identities

Prefixes below identify distinct whole-file SHA-256 hashes. Full hashes live
in the linked records; matching `.text` does not mean whole ELF files match.

| Executable | SHA-256 prefix | Evidence and meaning |
| --- | --- | --- |
| Installed `7c775ea` build | `96699d9db0d969bf` | [Plain](installed-sweep-20261015.json) and [numbered](installed-sweep-numbers-20261015.json) busy-workstation sweeps; accepted by `make verify` and Gitea run 421. |
| Previous installed baseline | `547021b9eb2c2069` | [Quiet predecessor comparison](spot-qualification.json), and the paired baseline of the 2026-10-15 sweeps; kept as `~/.local/bin/cgrep.prev-547021b9`. |
| Counting/filename predecessor | `0042aeb90ae90e94` | [Acceptance and diagnostics](count-output-followup.json), then [quiet comparison](spot-qualification.json); specific gains and a regression, not universal nonregression. |
| Accepted batched-writer build | `0e7c1cf2185f4ae2` | [Output-batch record](output-batch-followup.json); correctness accepted, performance unqualified. |
| Local batched-writer build | `7040733d8bf9845d` | [Output-batch record](output-batch-followup.json); executable text matched the accepted build, whole-file hash differs. |

The batched writer and review fixes were published as `52c5898` (Gitea
run 371) and `--version` as `7c775ea` (run 421). A successful Gitea run
does not verify later local changes.

Historical source manifests also cover recorder and verification scripts.
Tooling changes alone can make a live-tree recorder reject a historical
manifest while C sources and binaries stay unchanged. Reconstruct that record
against its matching frozen source snapshot; keep its original hashes.
Focused recorder tests do not reissue a full acceptance receipt.

## Current candidate and qualification attempts

| Record | What it establishes | What it does not establish |
| --- | --- | --- |
| [installed-sweep-20261015.json](installed-sweep-20261015.json), [installed-sweep-numbers-20261015.json](installed-sweep-numbers-20261015.json) | Validated paired 7-round medians of the installed build versus the previous one, ripgrep and GNU grep, with every sample. | Quiet-host qualification; the load average was 3-4.5. |
| [output-batch-followup.json](output-batch-followup.json) | Full isolated acceptance `verify.2MT7um`, 12,000 ordered comparisons, 91.59% / 91.78% coverage, diagnostic samples and instruction profiles. | Quiet native speedup, competitor parity or pthread semantic proof. |
| [batch-spot-attempt.json](batch-spot-attempt.json) | Three same-type Spot capacity failures, local smoke/controls and timestamped task cleanup checks. | A launched instance or any qualification timing. |
| [batch-alternate-attempt.json](batch-alternate-attempt.json) | Four alternate-type capacity failures, a separate invalid-bucket-name failure and task cleanup queries. | A launched instance, qualification timing, account-wide cleanup or invoice verification. |

The local 42-invocation smoke exercised one small fixture in plain/numbered
modes. It is not the planned 5,292-invocation qualification campaign.
Cloud harness fakes and local timeout/upload controls do not prove live guest
deadlines, SSM execution or final transfer behavior. Reviewer model identity
was unavailable; cross-model independence is not established.

## Predecessor measurements and diagnostics

| Record | Scope and limit |
| --- | --- |
| [spot-qualification.json](spot-qualification.json) | Completed quiet counting/filename-versus-installed comparison, 4,032 validated invocations, raw samples, selected profiles, failures and cleanup. Profiling was not separately quiet-qualified. |
| [count-output-followup.json](count-output-followup.json) | Predecessor acceptance `verify.y000FW`, 12,000 comparisons, 91.44% / 91.78% coverage, ablations and busy-workstation diagnostics. Its failed local qualification preceded the successful Spot comparison. |
| [numbering-prototypes.json](numbering-prototypes.json) | Scratch counting alternatives and failed local quiet admission; not production acceptance or full-search speedup. |
| [paired-quiet-attempt.json](paired-quiet-attempt.json) | Failed workstation admission; no timings or profiling started. |
| [performance-followup.json](performance-followup.json) | Earlier output/counting follow-up, accepted identities, full-output profiles and local pinned-source CI pilot. Contended timing and outer-wrapper timeout limits remain. |
| [performance-parallel.json](performance-parallel.json) | Historical opt-in-worker slice, fixed thread configurations and acceptance. Not quiet qualification of the current writer. |
| [performance-serial.json](performance-serial.json) | Historical serial optimization, acceptance, samples and controls. Not the current source or a universal performance claim. |
| [performance-baseline.json](performance-baseline.json) | Frozen pre-optimization baseline and profiles. Not a current executable comparison. |

Read [the performance history](PERFORMANCE-HISTORY-20261014.md) for the original
tables and interpretation. Historical present-tense claims apply to their
named source and executable, not automatically to today's working tree.

## CI artifact evidence

[qualification-artifacts.json](qualification-artifacts.json) records local
collection, retrieval, hash/status checks and a composite pilot using retained
selftest evidence. It does not prove a fresh wrapper run or live Gitea artifact
upload/download/retention. See [the CI runbook](../ci/README.md) for the
published run and remaining integration limits.

Artifact hashes establish integrity against the recorded manifest, not
uploader identity or the truth of every recorded claim. Required acceptance
receipts are a separate check. Bounded Cerberus semantics excludes native
pthread execution.

## Local raw artifacts and retention

These ignored directories retain executable copies, source snapshots, logs,
raw captures, helper scripts and inventories where recorded:

- `build/output-batch-20261014`
- `build/spot-qualification-20261014`
- `build/batch-spot-attempt-20261014`
- `build/batch-alternate-attempt-20261014`

Older records name additional original `/tmp` directories. Those paths are
local provenance, not durable remote storage. Neither ignored `build/` nor
`/tmp` is a backup or evidence of successful CI persistence. Preserve raw and
failed samples, receipts, frozen binaries and hashes. Before any future space
cleanup, establish a separately verified archive and migrate its references.
This documentation cleanup deletes, relocates and compresses none of that data.

## Archived notes

- [Performance snapshot](PERFORMANCE-HISTORY-20261014.md): complete pre-cleanup narrative, tables and recipes.
- [Quiet-benchmark snapshot](QUIET-BENCHMARKS-HISTORY-20261014.md): the original admission/prototype note.
- [Work-loop snapshot](../.pi/work-loop-history-20261014.md): complete pre-cleanup task/approval chronology.

These snapshots preserve the previous Markdown bytes. Their old plans and
cloud approvals are historical context, not permission to resume provisioning,
installation, publication or optimization.
