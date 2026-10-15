# Gitea acceptance CI

The workflow runs full acceptance for trusted `trav` pushes to `trav/cgrep`
`main`. It has no deployment or pull-request trigger. The repository-scoped
`cgrep-x86_64-3g:host` runner executes on Linux x86_64. Do not expose this
personal host runner to untrusted code. It is not a container sandbox and can
access the runner user's home and credentials.

## Current setup

Gitea has private cgrep and dependable-c-agent-toolchain copies of their
previously published main commits. Runner 441, `cgrep-workstation`, is
registered only for cgrep. Its user service is `act_runner-cgrep.service`;
the existing general Docker runner was not changed.

The dedicated runner has capacity one, nice 15, a 3 GiB memory limit, no
swap, and core dumps disabled. The workflow calls `bash ci/verify.sh`.
That script also serializes local cgrep acceptance jobs, runs toolchain
selftests in a capped user scope, and calls resource-capped `make verify`.
Failures survive the `tee` logging pipelines through `set -euo pipefail`.

Checks are sequential. The selftest scope has a deadline; acceptance has a
30-minute timeout. A timed-out required check fails CI rather than becoming
a successful skip.

Before publication, the local entry-point pilot fetched Gitea source,
validated its trees, passed toolchain selftests, and reached full acceptance
PASS. Its outer tool call timed out before the wrapper returned, so that
pilot's wrapper exit status is not verified. This is not a server-triggered
CI result. Check the job for the published commit separately. See
`../docs/performance-followup.json` for retained local evidence.

## Runner prerequisites

Provision the existing dependable-c-agent-toolchain checker profile before
accepting jobs. CI calls `dcheck-tools check`; it will not install replacement
checkers or fall back to defective global versions. Required compiler,
Python, git, `tea`, zig, wasmtime, clang tooling, ripgrep, GNU grep, and user
systemd commands must remain on the runner's PATH.

Runner configuration is local at `~/.config/act_runner-cgrep/config.yaml`.
Registration credentials are not repository files. The host uses its existing
`tea` login `trav` to read the two private dependency archives over HTTPS.
Direct SSH clones timed out on this host. CI never copies the tea credential
into the checkout, environment, or artifacts.

`dependencies.json` pins both complete commit IDs and Git tree IDs.
`fetch_dependencies.py` rejects archive links, traversal, Git metadata,
duplicate paths, and oversized inputs. It reconstructs each Git tree and
rejects mismatches before running code. Update commit and tree pins together
only after verifying the new dependency. This checks source contents, not
commit signatures or the identity of the Gitea operator.

## Inspect a run

CI streams checks into the job log. Its temporary scratch directory contains
logs, pinned source, and JSON receipts while the job runs. The runner removes
that directory after completion; run 330 confirmed this cleanup behavior.
Do not rely on the printed scratch path for post-run evidence.

The EXIT handler copies only `toolchain-selftest.log`, `acceptance.log`,
`c-utils.json`, `cli.json`, `cgrep.json`, and a hash/status manifest to
`ci-artifacts/`. It marks incomplete failures with their original exit code.
A successful run missing required evidence fails collection. The collector
never copies source trees, home directories, tea configuration, or runner
credential files. Logs and receipts come only from trusted checks.
The upload step pins v3.1.3 with its node16 action descriptor and legacy
artifact protocol, rather than assuming the installed runner supports
node24 or the v4 protocol. It runs on success or ordinary failure, errors on
missing files, and requests 30-day retention. A runner kill or exhausted job
timeout can prevent collection/upload; server job logs remain the fallback. Download the `acceptance-SHA-RUN_ID` artifact
from the Gitea job. Live upload remains unverified until this workflow revision
is published and exercised against Gitea; local collection/sensitivity checks
are not an upload claim.

After extracting a downloaded artifact into a fresh directory, check the
directory that contains `metadata.json`:

```sh
python3 -I ci/check_artifacts.py /path/to/extracted-bundle
# A successful bundle must also satisfy the current required receipt policy:
python3 -I ci/check_artifacts.py /path/to/extracted-bundle --receipts
```

The first command verifies the exact inventory, sizes, hashes, and recorded
status. It can succeed for a valid failure bundle and prints that failure's
exit code. `--receipts` refuses failed acceptance and checks the dependency,
CLI, and deep receipts. Both commands reject symlinks, unlisted files, missing
successful evidence, and inconsistent status. Hashes check contents, not the
uploader's identity or the truth of a forged manifest.

The latest local pilot used real count/filename acceptance receipts with a
retained selftest log from the unchanged pinned toolchain. It exercised the
actual EXIT collector, successful collection, seeded failure preservation,
and local ZIP retrieval after disposable job cleanup. This is explicitly a
composite local pilot, not a fresh CI wrapper run or live Gitea retrieval.
Its sources and results are retained in
`../docs/qualification-artifacts.json`. Quiet benchmark admission still failed;
no qualification timings started.

A required PASS means both the process and receipt checks succeeded. Optional authority, regions, absent
CBMC proofs, and tcc exclusions remain explicit. Semantics is bounded and
does not interpret pthreads.

For a local pilot with the same source acquisition and commands:

```sh
mkdir -p /tmp/cgrep-ci-pilot
RUNNER_TEMP=/tmp/cgrep-ci-pilot bash ci/verify.sh
```

Use a fresh checkout and the provisioned runner environment. Local pilots
leave `ci-artifacts/` for inspection; another run refuses that existing path
rather than overwriting evidence. Never run a second heavy acceptance check
alongside it. Check `systemctl --user status act_runner-cgrep.service`
for runner health. The service being active does not prove a workflow ran;
verify the Gitea job log and the acceptance receipts after publishing.
