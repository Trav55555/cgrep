#!/usr/bin/env bash
# Trusted Gitea runs only. A repository-scoped host runner owns this profile.
# Usage: ci/verify.sh gate|deep. A push runs gate; the nightly runs deep and
# reports its findings (ci/README.md).
set -euo pipefail
ulimit -c 0
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
tier="${1:-}"
[[ "$tier" == gate || "$tier" == deep ]] || { echo 'usage: ci/verify.sh gate|deep' >&2; exit 2; }
[[ "$(uname -s)" == Linux && "$(uname -m)" == x86_64 ]] || { echo 'CI requires Linux x86_64' >&2; exit 1; }
: "${XDG_RUNTIME_DIR:?user systemd runtime required}"
: "${RUNNER_TEMP:?runner scratch required}"
command -v flock >/dev/null
command -v systemd-run >/dev/null
command -v timeout >/dev/null
command -v tea >/dev/null
[[ ! -e "$root/ci-artifacts" && ! -L "$root/ci-artifacts" ]] || {
    echo 'CI artifact destination already exists; use a fresh checkout' >&2
    exit 1
}
systemctl --user show-environment >/dev/null
# Serialize acceptance runs even if this repository gains another runner.
exec 9>"$XDG_RUNTIME_DIR/cgrep-acceptance.lock"
flock 9
work="$(mktemp -d "$RUNNER_TEMP/cgrep-acceptance.XXXXXX")"
printf 'CI scratch: %s\n' "$work"
# EXIT collection runs after pass/failure, before the runner removes its temp.
# Preserve the check's original failure; a collection failure also fails CI.
# A deep run then judges itself with ci/findings.py, still holding the lock,
# and keeps the findings issue; a Gitea failure there does not change the
# verdict. Gitea gets 10 minutes, inside the job's timeout-minutes.
collect_receipts() {
    status=$?
    trap - EXIT
    if ! python3 -I "$root/ci/collect_artifacts.py" "$work" "$root/ci-artifacts" \
        --status "$status" --revision "${GITHUB_SHA:-}" --tier "$tier"; then
        if [[ "$status" -eq 0 ]]; then status=1; fi
    fi
    if [[ "$tier" == deep ]]; then
        log="$work/acceptance.log"
        [[ -f "$log" ]] || log="$work/toolchain-selftest.log"
        issue=()
        if [[ -n "${GITHUB_REPOSITORY:-}" ]]; then
            issue=(--login trav --repo "$GITHUB_REPOSITORY" --within 600)
        fi
        # The checks tests/check_receipt.py requires at deep, so none passes skipped.
        verdict=0
        python3 -I "$root/ci/findings.py" --result "$root/ci-artifacts/cgrep.json" \
            --root "$work/cgrep" --log "$log" --exit-status "$status" \
            --require subset --require lint --require matrix --require analyzer \
            --require sanitize --require msan --require release --require wasm \
            --require coverage --require semantics --require valgrind --require fuzz \
            --rev "${GITHUB_SHA:-unknown}" \
            --run-url "${GITHUB_SERVER_URL:-}/${GITHUB_REPOSITORY:-}/actions/runs/${GITHUB_RUN_ID:-}" \
            "${issue[@]}" || verdict=$?
        if [[ "$status" -eq 0 && "$verdict" -ne 0 ]]; then status=1; fi
    fi
    exit "$status"
}
trap collect_receipts EXIT
cp -a "$root" "$work/cgrep"
python3 -I "$root/ci/fetch_dependencies.py" "$root/ci/dependencies.json" "$work"
toolchain="$work/dependable-c-agent-toolchain"
# Validate provisioned tools against the checked-in source policy, not just PATH.
# Provisioning is separate; no silently downloaded/unpinned CI installers.
"$toolchain/bin/dcheck-tools" check
"$toolchain/bin/dcheck-tools" exec systemd-run --user --scope -q \
    -p MemoryMax=3G -p MemorySwapMax=0 -p RuntimeMaxSec=1200 nice -n 15 \
    make -j1 -C "$toolchain" selftest 2>&1 | tee "$work/toolchain-selftest.log"
grep -Fx 'selftest: PASS' "$work/toolchain-selftest.log"
# Deep adds Cerberus twice (serial_sensitivity.py and dcheck deep); in its
# first run, 30 minutes ended inside dcheck deep.
case "$tier" in gate) limit=30m ;; deep) limit=100m ;; esac
python3 -I "$work/cgrep/ci/reference.py" exec -- \
    timeout --signal=TERM --kill-after=30s "$limit" make -j1 -C "$work/cgrep" verify \
    CUTILS="$work/c-utils" DCHECK="$toolchain/bin/dcheck" VERIFY_TIER="$tier" \
    2>&1 | tee "$work/acceptance.log"
grep -F 'acceptance: PASS ' "$work/acceptance.log"
printf 'CI acceptance: PASS (%s)\n' "$tier"
