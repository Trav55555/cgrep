#!/usr/bin/env bash
# Trusted Gitea push only. A repository-scoped host runner owns this profile.
set -euo pipefail
ulimit -c 0
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ "$(uname -s)" == Linux && "$(uname -m)" == x86_64 ]] || { echo 'CI requires Linux x86_64' >&2; exit 1; }
: "${XDG_RUNTIME_DIR:?user systemd runtime required}"
: "${RUNNER_TEMP:?runner scratch required}"
command -v flock >/dev/null
command -v systemd-run >/dev/null
command -v timeout >/dev/null
command -v tea >/dev/null
systemctl --user show-environment >/dev/null
# Serialize acceptance runs even if this repository gains another runner.
exec 9>"$XDG_RUNTIME_DIR/cgrep-acceptance.lock"
flock 9
work="$(mktemp -d "$RUNNER_TEMP/cgrep-acceptance.XXXXXX")"
printf 'CI artifacts: %s\n' "$work"
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
timeout --signal=TERM --kill-after=30s 30m make -j1 -C "$work/cgrep" verify \
    CUTILS="$work/c-utils" DCHECK="$toolchain/bin/dcheck" \
    2>&1 | tee "$work/acceptance.log"
grep -F 'acceptance: PASS ' "$work/acceptance.log"
printf 'CI acceptance: PASS\n'
