#!/usr/bin/env bash
set -eu
CUTILS="$(cd "${CUTILS:-../c-utils}" && pwd)"
export CUTILS
set +o posix
ulimit -c 0
ulimit -f 16384
files=(--file src/cgrep.c --file src/ignore.inc)
command='(ulimit -v 786432; make -s -B CUTILS="$CUTILS" build/test_ignore build/test_walk build/cgrep && timeout 30 build/test_ignore && timeout 30 build/test_walk && python3 -I tests/ignore_difftest.py build/cgrep --cases 150 && make -s -B CUTILS="$CUTILS" test) && ASAN_OPTIONS=detect_leaks=0:hard_rss_limit_mb=512 make -s -B CUTILS="$CUTILS" CC=clang BUILD=build-sanitize CFLAGS="-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all" test && make -s -B CUTILS="$CUTILS" CC=clang BUILD=build-msan CFLAGS="-g -O1 -fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer" test && python3 -I tests/difftest.py build/cgrep --cases 150'
if [[ -n "${MUTATION_FILE:-}" ]]; then
    case "$MUTATION_FILE" in
        src/cgrep.c|src/ignore.inc) ;;
        *) printf '%s\n' 'MUTATION_FILE must be src/cgrep.c or src/ignore.inc' >&2; exit 2 ;;
    esac
    files=(--file "$MUTATION_FILE")
    if [[ "$MUTATION_FILE" == src/ignore.inc ]]; then
        command='(ulimit -v 786432; make -s -B CUTILS="$CUTILS" build/test_ignore build/test_walk build/cgrep && timeout 30 build/test_ignore && timeout 30 build/test_walk && python3 -I tests/ignore_difftest.py build/cgrep --cases 150) && ASAN_OPTIONS=detect_leaks=0:hard_rss_limit_mb=512 make -s -B CUTILS="$CUTILS" CC=clang BUILD=build-sanitize CFLAGS="-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all" build-sanitize/test_ignore build-sanitize/test_walk && ASAN_OPTIONS=detect_leaks=0:hard_rss_limit_mb=512 timeout 30 build-sanitize/test_ignore && ASAN_OPTIONS=detect_leaks=0:hard_rss_limit_mb=512 timeout 30 build-sanitize/test_walk && make -s -B CUTILS="$CUTILS" CC=clang BUILD=build-msan CFLAGS="-g -O1 -fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer" build-msan/test_ignore build-msan/test_walk && timeout 30 build-msan/test_ignore && timeout 30 build-msan/test_walk && python3 -I tests/difftest.py build/cgrep --cases 150'
    fi
fi
exec nice -n 15 python3 "${MUTANTS:-$HOME/.agents/skills/c-verification/scripts/c_mutants.py}" \
    --root . "${files[@]}" --jobs "${JOBS:-2}" --timeout "${MUTATION_TIMEOUT:-30}" \
    --build 'cc -std=c89 -fsyntax-only -Iinclude -I"$CUTILS/include" src/cgrep.c' \
    --cmd "$command"
