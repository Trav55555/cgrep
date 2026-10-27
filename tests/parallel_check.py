"""Native worker lifetime/race checks and compiled behavioral sensitivity.

Run inside the acceptance memory scope. No unavailable-tool success or race
suppression; instrument the actual production scanner and worker implementation.
"""
import argparse
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def build(project, cutils, binary, sanitizer):
    command = ["/usr/bin/gcc", "-std=c89", "-pedantic-errors", "-Wall", "-Wextra", "-Werror",
               "-D_GNU_SOURCE", "-g", "-O1", f"-fsanitize={sanitizer}", "-pthread",
               "-I" + str(project / "include"), "-I" + str(cutils / "include"),
               str(project / "tests/test_parallel.c"),
               *map(str, sorted((project / "src").glob("*.c"))),
               *map(str, sorted((cutils / "src").glob("*.c"))), "-o", str(binary)]
    subprocess.run(command, check=True, capture_output=True, timeout=90)


def run(binary, sanitizer, reject=None):
    env = dict(os.environ, TSAN_OPTIONS="halt_on_error=1", ASAN_OPTIONS="detect_leaks=1:halt_on_error=1")
    try:
        result = subprocess.run([str(binary), "--native-required"], env=env, capture_output=True,
                                timeout=8 if reject == "hang" else 60)
    except subprocess.TimeoutExpired:
        if reject == "hang":
            return
        raise
    if reject == "lifetime":
        if result.returncode == 0 or not (result.returncode == -signal.SIGABRT or
                b"Assertion" in result.stderr or b"heap-use-after-free" in result.stderr):
            raise RuntimeError(f"compiled mutant missed its lifetime sensor: {result!r}")
    elif reject:
        if result.returncode == 0 or b"Assertion" not in result.stderr:
            raise RuntimeError(f"compiled mutant missed its assertion sensor: {result!r}")
    elif result.returncode != 0 or result.stderr or b"test_parallel: native PASS" not in result.stdout:
        raise RuntimeError(f"{sanitizer} worker check failed: {result!r}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cutils", type=Path, default=ROOT.parent / "c-utils")
    args = parser.parse_args()
    cutils = args.cutils.resolve()
    with tempfile.TemporaryDirectory(prefix="cgrep-parallel-check-") as directory:
        work = Path(directory)
        binary = work / "race-unit"
        build(ROOT, cutils, binary, "thread")
        run(binary, "ThreadSanitizer")
        print("PASS native worker failure/lifetime paths: ThreadSanitizer", flush=True)
        race = work / "race.c"
        race.write_text('#include <pthread.h>\nstatic volatile int value;\n'
                        'static pthread_barrier_t start;\n'
                        'static void *writer(void *p) { int i; (void)p; pthread_barrier_wait(&start); '
                        'for (i = 0; i < 100000; i++) value = 1; return 0; }\n'
                        'int main(void) { pthread_t a; pthread_t b; '
                        'if (pthread_barrier_init(&start, 0, 3)) return 2; '
                        'if (pthread_create(&a, 0, writer, 0) || pthread_create(&b, 0, writer, 0)) return 2; '
                        'pthread_barrier_wait(&start); '
                        'if (pthread_join(a, 0) || pthread_join(b, 0)) return 2; '
                        'pthread_barrier_destroy(&start); return value == 0; }\n')
        probe = work / "race-probe"
        subprocess.run(["/usr/bin/gcc", "-std=c89", "-D_GNU_SOURCE", "-O1", "-g", "-fsanitize=thread", "-pthread",
                        str(race), "-o", str(probe)], check=True, capture_output=True)
        for _ in range(3):
            result = subprocess.run([str(probe)], env=dict(os.environ, TSAN_OPTIONS="halt_on_error=1"),
                                    capture_output=True, timeout=20)
            if result.returncode == 0 or b"ThreadSanitizer: data race" not in result.stderr:
                raise RuntimeError(f"race detector failed its negative control: {result!r}")
        print("PASS race detector rejects compiled unsynchronized-write control: 3/3", flush=True)
        binary = work / "memory-unit"
        build(ROOT, cutils, binary, "address,undefined")
        run(binary, "ASan/UBSan/LSan")
        print("PASS native worker failure/lifetime paths: ASan/UBSan/LSan", flush=True)
        for filename, name, old, new, rejection in [
            ("parallel.inc", "file-reordering", "worker->paths[f] = paths[start + f];",
             "worker->paths[f] = paths[pool->count - 1 - (start + f)];", "assert"),
            ("parallel.inc", "lost-input-status",
             'fprintf(err, "cgrep: %s: %s\\n", record->path, scan_error(record->result));\n                *failed = 1;',
             'fprintf(err, "cgrep: %s: %s\\n", record->path, scan_error(record->result));\n                *failed = 0;',
             "assert"),
            ("parallel.inc", "missing-cancel", "    pool_cancel(pool, pool->started);",
             "    if (0) pool_cancel(pool, pool->started);", "hang"),
            ("parallel.inc", "missing-join", "        cu_thread_join(pool->workers[i]->thread);",
             "        if (0) cu_thread_join(pool->workers[i]->thread);", "lifetime"),
            ("parallel.inc", "missing-condition-destroy", "        cu_cond_destroy(pool->workers[i]->changed);",
             "        if (0) cu_cond_destroy(pool->workers[i]->changed);", "assert"),
            ("parallel.inc", "missing-mutex-destroy", "    cu_cond_destroy(pool->changed);\n    cu_mutex_destroy(pool->mutex);",
             "    cu_cond_destroy(pool->changed);\n    if (0) cu_mutex_destroy(pool->mutex);", "assert"),
            ("parallel.inc", "lost-startup-status",
             'fprintf(err, "cgrep: worker startup: %s\\n", strerror(code));\n            *failed = 1;',
             'fprintf(err, "cgrep: worker startup: %s\\n", strerror(code));', "assert"),
            ("parallel.inc", "lost-serial-fallback",
             "        code = errno;\n        cu_arena_release(&thread_arena);\n        CGREP_POOL_FREE(pool);\n        if (code == ENOSYS)\n            return 0;",
             "        code = errno;\n        cu_arena_release(&thread_arena);\n        CGREP_POOL_FREE(pool);\n        if (0 && code == ENOSYS)\n            return 0;",
             "assert"),
            ("parallel.inc", "wrong-worker-stack", "#define WORKER_STACK ((size_t)262144)",
             "#define WORKER_STACK ((size_t)524288)", "assert"),
            ("cgrep.c", "wrong-body-fragment",
             "parts[part_count++] = cu_str_n(text.ptr + start, output_len);",
             "parts[part_count++] = cu_str_n(text.ptr + start, output_len - 1);", "assert"),
            ("cgrep.c", "ignored-batch-failure",
             "if (!emit(s, parts, part_count))",
             "if (emit(s, parts, part_count) < 0)", "assert"),
            ("parallel.inc", "short-channel-progress",
             "worker->used = at;", "worker->used = at - 1;", "assert"),
            ("parallel.inc", "missing-byte-progress",
             "worker->bytes[at++] = parts[i].ptr[0];",
             "worker->bytes[at] = parts[i].ptr[0];", "assert"),
        ]:
            project = work / name
            for folder in ["src", "include"]:
                shutil.copytree(ROOT / folder, project / folder)
            (project / "tests").mkdir()
            shutil.copy2(ROOT / "tests/test_parallel.c", project / "tests/test_parallel.c")
            path = project / "src" / filename
            text = path.read_text()
            if text.count(old) != 1:
                raise RuntimeError("stale worker mutation: " + name)
            path.write_text(text.replace(old, new))
            binary = project / "mutant"
            build(project, cutils, binary, "address,undefined")
            run(binary, "mutant", rejection)
            print("PASS compiled worker mutant rejected:", name, flush=True)


if __name__ == "__main__":
    main()
