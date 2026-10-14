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

CI streams checks into the job log and retains scratch directories under
`$RUNNER_TEMP/cgrep-acceptance.*`. The script prints the scratch path first. It
contains `toolchain-selftest.log`, `acceptance.log`, pinned sibling sources,
and `cgrep/build/verify.*/` JSON receipts. A required PASS means both the
process and receipt checks succeeded. Optional authority, regions, absent
CBMC proofs, and tcc exclusions remain explicit. Semantics is bounded and
does not interpret pthreads.

For a local pilot with the same source acquisition and commands:

```sh
mkdir -p /tmp/cgrep-ci-pilot
RUNNER_TEMP=/tmp/cgrep-ci-pilot bash ci/verify.sh
```

Use the provisioned runner environment. Never run a second heavy acceptance
check alongside it. Check `systemctl --user status act_runner-cgrep.service`
for runner health. The service being active does not prove a workflow ran;
verify the Gitea job log and the acceptance receipts after publishing.
