#!/usr/bin/env bash
# Each worker rebuilds in its own scratch copy. Bound disk output as well
# as time and native memory: a loop mutant can fill TMPDIR through stdio.
set -eu
CUTILS="$(cd "${CUTILS:-../c-utils}" && pwd)"
export CUTILS
set +o posix
ulimit -c 0
ulimit -f 16384
exec nice -n 15 python3 "${MUTANTS:-$HOME/.agents/skills/c-verification/scripts/c_mutants.py}" \
    --root . --file src/cgrep.c --jobs "${JOBS:-2}" --timeout "${MUTATION_TIMEOUT:-30}" \
    --build 'cc -std=c89 -fsyntax-only -Iinclude -I"$CUTILS/include" src/cgrep.c' \
    --cmd '(ulimit -v 786432; make -s -B CUTILS="$CUTILS" test build/cgrep) && ASAN_OPTIONS=detect_leaks=0:hard_rss_limit_mb=512 make -s -B CUTILS="$CUTILS" CC=clang BUILD=build-sanitize CFLAGS="-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all" test && make -s -B CUTILS="$CUTILS" CC=clang BUILD=build-msan CFLAGS="-g -O1 -fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer" test && python3 -I tests/difftest.py build/cgrep --cases 150'
