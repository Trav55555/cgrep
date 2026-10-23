#!/usr/bin/env bash
# The default entry point always creates a resource-limited scope. No uncapped
# fallback: this machine has exhausted memory in previous verification runs.
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
case "${1:-}" in
  --scoped)
    dcheck="${DCHECK:-$HOME/projects/dependable-c-agent-toolchain/bin/dcheck}"
    tools="$(dirname "$(readlink -f "$dcheck")")/dcheck-tools"
    exec "$tools" exec bash "$root/tests/verify.sh" --profiled ;;
  --profiled) ;;
  *)
    exec systemd-run --user --scope -q -p MemoryMax=3G -p MemorySwapMax=0 \
      nice -n 15 bash "$root/tests/verify.sh" --scoped ;;
esac
ulimit -c 0
cd "$root"
CUTILS="$(cd "${CUTILS:-../c-utils}" && pwd)"
DCHECK="${DCHECK:-$HOME/projects/dependable-c-agent-toolchain/bin/dcheck}"
BUILD="${BUILD:-build}"
mkdir -p "$BUILD"
receipt="$(mktemp -d "$BUILD/verify.XXXXXX")"
receipt="$(cd "$receipt" && pwd)"
printf 'Acceptance receipts: %s\n' "$receipt"

# Dependency tests are not included by dcheck's DEPS linker.
make -j1 -C "$CUTILS" test
(cd "$CUTILS" && "$DCHECK" gate --json) > "$receipt/c-utils.json" || {
  python3 -I tests/check_receipt.py "$receipt/c-utils.json" gate; exit 1;
}
python3 -I tests/check_receipt.py "$receipt/c-utils.json" gate

"$DCHECK" fast cmd/main.c --json > "$receipt/cli.json" || {
  python3 -I tests/check_receipt.py "$receipt/cli.json" fast; exit 1;
}
python3 -I tests/check_receipt.py "$receipt/cli.json" fast
python3 -I tests/fuzz_seeds.py --check
python3 -I tests/test_receipt.py
python3 -I tests/test_recorders.py
python3 -I tests/mutation_selector_check.py tests/mutate.sh --root .
python3 -I tests/framing_check.py --cutils "$CUTILS" --sensitivity
python3 -I tests/serial_sensitivity.py --cutils "$CUTILS" --cerberus "$DCHECK_CERBERUS"
python3 -I tests/test_parallel_benchmark.py
python3 -I tests/test_realistic_benchmark.py
python3 -I tests/test_quiet_benchmark.py
python3 -I tests/test_ci.py
python3 -I tests/test_artifacts.py
python3 -I tests/parallel_check.py --cutils "$CUTILS"
"$DCHECK" deep --json > "$receipt/cgrep.json" || {
  python3 -I tests/check_receipt.py "$receipt/cgrep.json" deep; exit 1;
}
python3 -I tests/check_receipt.py "$receipt/cgrep.json" deep
make -j1 -B CUTILS="$CUTILS" "$BUILD/cgrep"
python3 -I tests/io_test.py "$BUILD/cgrep"
python3 -I tests/difftest.py "$BUILD/cgrep" --cases 3000 --seed 7
python3 -I tests/difftest.py "$BUILD/cgrep" --cases 3000 --seed 42
python3 -I tests/difftest.py "$BUILD/cgrep" --cases 3000 --seed 7 --jobs 8
python3 -I tests/difftest.py "$BUILD/cgrep" --cases 3000 --seed 42 --jobs 8
python3 -I tests/ignore_difftest.py "$BUILD/cgrep" --cases 1000 --seed 7
python3 -I tests/ignore_difftest.py "$BUILD/cgrep" --cases 1000 --seed 42 --jobs 8
python3 -I tests/long_difftest.py "$BUILD/cgrep" --cases 60 --seed 7
printf 'acceptance: PASS (optional authority/regions/CBMC checks may be skipped; semantics is bounded)\n'
