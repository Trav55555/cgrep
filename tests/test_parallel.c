#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "cu.h"

#if defined(__linux__) && defined(__GLIBC__) && defined(_GNU_SOURCE) && !defined(__cerb__)
#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

enum { F_NONE, F_POOL, F_WORKERS, F_ARENA, F_OPEN, F_STAT, F_FDOPEN,
       F_WINDOW, F_READ, F_CLOSE, F_SERIAL, F_FIFO, F_REPLACE, F_WALK, F_STOP,
       F_MEMSTREAM, F_SERIAL_FAIL };
static int fault;
static int allocations;
static int creates;
static int started_threads;
static int joined;
static int mutexes;
static int destroyed_mutexes;
static int conditions;
static int destroyed_conditions;
static size_t startup_attempts;
static size_t startup_failure = (size_t)-1;
static size_t unavailable_at = (size_t)-1;
static const char *fault_path;
static pthread_mutex_t race_mutex;
static pthread_cond_t race_changed;
static int race_ready;
/* The pool of the current invocation (its first allocation) and what it
 * recorded when freed. */
static void *captured_pool;
static unsigned long last_claims;
static int last_started;
static int subdir_opens; /* directories opened below count_root */
static const char *count_root;
static int test_batch_start = 1;

static void *test_alloc(size_t n)
{
    void *p;

    allocations++;
    if (fault == F_POOL || (fault == F_WORKERS && allocations == 2))
        return NULL;
    p = malloc(n);
    if (allocations == 1)
        captured_pool = p;
    return p;
}

static int unavailable(void)
{
    if (startup_attempts++ == unavailable_at) {
        errno = ENOSYS;
        return 1;
    }
    return 0;
}

static int test_mutex_init(CuMutex **m, CuArena *a)
{
    int ok;

    if (unavailable())
        return 0;
    if (fault == F_ARENA)
        cu_arena_fail_at(a, a->attempts);
    ok = cu_mutex_init(m, a);
    if (ok)
        mutexes++;
    return ok;
}

static void test_mutex_destroy(CuMutex *m)
{
    cu_mutex_destroy(m);
    destroyed_mutexes++;
}

static int test_cond_init(CuCond **c, CuArena *a)
{
    int ok;

    if (unavailable())
        return 0;
    ok = cu_cond_init(c, a);
    if (ok)
        conditions++;
    return ok;
}

static void test_cond_destroy(CuCond *c)
{
    cu_cond_destroy(c);
    destroyed_conditions++;
}

static int test_create(CuThread **t, CuArena *a, size_t stack_size,
                       void (*fn)(void *), void *context)
{
    int ok;

    creates++;
    assert(stack_size == (size_t)262144 || stack_size == (size_t)1048576);
    if (unavailable())
        return 0;
    ok = cu_thread_start(t, a, stack_size, fn, context);
    if (ok)
        started_threads++;
    return ok;
}

static void test_join(CuThread *thread)
{
    cu_thread_join(thread);
    joined++;
}

static int test_open(const char *path, int flags)
{
    int fd;

    if ((fault == F_FIFO && strcmp(path, fault_path) != 0) || fault == F_STOP) {
        assert(pthread_mutex_lock(&race_mutex) == 0);
        while (!race_ready)
            assert(pthread_cond_wait(&race_changed, &race_mutex) == 0);
        assert(pthread_mutex_unlock(&race_mutex) == 0);
    }
    if (fault_path != NULL && strcmp(path, fault_path) == 0) {
        if (fault == F_OPEN) {
            errno = EACCES;
            return -1;
        }
        if (fault == F_SERIAL)
            return open("/tmp", flags);
        if (fault == F_SERIAL_FAIL) {
            /* Non-regular for the worker, unreadable for the coordinator. */
            assert(chmod(path, 0) == 0);
            return open("/tmp", flags);
        }
        if (fault == F_FIFO) {
            assert(unlink(path) == 0);
            assert(mkfifo(path, 0600) == 0);
            assert(pthread_mutex_lock(&race_mutex) == 0);
            race_ready = 1;
            assert(pthread_cond_broadcast(&race_changed) == 0);
            assert(pthread_mutex_unlock(&race_mutex) == 0);
        }
    }
    fd = open(path, flags);
    if (fd >= 0 && fault == F_REPLACE && fault_path != NULL && strcmp(path, fault_path) == 0) {
        assert(unlink(path) == 0);
        assert(mkdir(path, 0700) == 0);
    }
    return fd;
}

static CuInfoResult test_stat(int fd, CuFileInfo *info)
{
    if (fault == F_STAT) {
        errno = EIO;
        return CU_INFO_ERROR;
    }
    return cu_fd_info(fd, info);
}

static FILE *test_fdopen(int fd, const char *mode)
{
    if (fault == F_FDOPEN) {
        errno = ENOMEM;
        return NULL;
    }
    return fdopen(fd, mode);
}

static FILE *test_memstream(char **text, size_t *size)
{
    if (fault == F_MEMSTREAM) {
        errno = ENOMEM;
        return NULL;
    }
    return open_memstream(text, size);
}

static size_t reserve_calls;
static size_t reserve_fail_at; /* 0: none; n: the nth reserve since set */

static int test_reserve(CuVec *buffer, size_t extra)
{
    if (fault == F_WINDOW || (reserve_fail_at != 0 && ++reserve_calls == reserve_fail_at)) {
        buffer->failed = 1;
        return 0;
    }
    return cu_vec_reserve(buffer, extra);
}

static CuInputStatus test_read(CuInput *in, void *bytes, size_t cap, size_t *n)
{
    if (fault == F_READ) {
        in->state = CU_INPUT_ERROR;
        in->error = EIO;
        *n = 0;
        return CU_INPUT_ERROR;
    }
    return cu_input_read(in, bytes, cap, n);
}

static int test_close(FILE *file)
{
    struct stat st;
    int fail;
    int r;

    fail = (fault == F_CLOSE || fault == F_READ) && fileno(file) >= 0 &&
        fstat(fileno(file), &st) == 0 && S_ISREG(st.st_mode) &&
        (fcntl(fileno(file), F_GETFL) & O_ACCMODE) == O_RDONLY;
    r = fclose(file);
    if (fail) {
        errno = fault == F_READ ? ENOSPC : EIO;
        return EOF;
    }
    return r;
}

/* Two input chunks per slot: the same slot logic as the 1 MiB production
 * size, with fixtures small enough for valgrind's per-test time limit. */
#define CHANNEL_BYTES 131072
/* Every unclaimed input starts a worker, so small fixtures use workers. */
#define CGREP_START_BACKLOG 1
#define CGREP_BATCH_START test_batch_start
/* The walker lists alone: thread and lock counts below are the pool's.
 * tests/test_prefetch.c covers listing ahead. */
#define CGREP_PREFETCH 0
#define CGREP_POOL_ALLOC test_alloc
#define CGREP_POOL_FREE test_free
#define cu_thread_start test_create
#define cu_thread_join test_join
#define cu_mutex_init test_mutex_init
#define cu_mutex_destroy test_mutex_destroy
#define cu_cond_init test_cond_init
#define cu_cond_destroy test_cond_destroy
#define open test_open
#define cu_fd_info test_stat
#define fdopen test_fdopen
#define cu_vec_reserve test_reserve
#define cu_input_read test_read
#define fclose test_close
#define open_memstream test_memstream
#define cu_dir_open test_dir_open
/* The pool is defined by the included source; these hooks read it. */
struct ParallelPool;
static void test_free(void *p);
static int test_dir_open(CuDir *d, CuArena *a, const char *path);
#endif
#define cgrep_main parallel_cli
#include "../src/cgrep.c"

/* The printing selection the CLI runs without -c, -l or -v. */
static int search_text(const Search *s, const char *name, CuStr text, unsigned long *lines)
{
    return select_text(s, name, text, text, lines, SELECT_PRINT);
}
#undef cgrep_main
#ifdef CGREP_NATIVE_PARALLEL
#undef cu_thread_start
#undef cu_thread_join
#undef cu_mutex_init
#undef cu_mutex_destroy
#undef cu_cond_init
#undef cu_cond_destroy
#undef open
#undef cu_fd_info
#undef fdopen
#undef cu_vec_reserve
#undef cu_input_read
#undef fclose
#undef open_memstream
#undef cu_dir_open

static void test_free(void *p)
{
    ParallelPool *pool;

    if (p == captured_pool) {
        pool = (ParallelPool *)p;
        last_claims = pool->claims;
        last_started = pool->started;
        captured_pool = NULL;
    }
    free(p);
}

/* Under F_WALK, listing fault_path waits until every input published so
 * far has been claimed: the search is running while the walk is not
 * finished. Under F_STOP, it lets the workers open their inputs
 * (test_open), then waits for cancellation. */
static int test_dir_open(CuDir *d, CuArena *a, const char *path)
{
    ParallelPool *pool;

    if (fault_path != NULL && strcmp(path, fault_path) == 0 && (fault == F_WALK || fault == F_STOP)) {
        pool = (ParallelPool *)captured_pool;
        assert(pool != NULL);
        if (fault == F_STOP) {
            assert(pthread_mutex_lock(&race_mutex) == 0);
            race_ready = 1;
            assert(pthread_cond_broadcast(&race_changed) == 0);
            assert(pthread_mutex_unlock(&race_mutex) == 0);
        }
        cu_mutex_lock(pool->mutex);
        while (fault == F_WALK ? !(pool->count > 0 && pool->next == pool->count) : !pool->cancel)
            cu_cond_wait(pool->changed, pool->mutex);
        cu_mutex_unlock(pool->mutex);
    }
    if (count_root != NULL && strncmp(path, count_root, strlen(count_root)) == 0 &&
        path[strlen(count_root)] == '/')
        subdir_opens++;
    return cu_dir_open(d, a, path);
}

static int descriptors(void)
{
    DIR *dir;
    struct dirent *entry;
    int count;

    dir = opendir("/proc/self/fd");
    assert(dir != NULL);
    count = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0)
            count++;
    }
    assert(closedir(dir) == 0);
    return count;
}

static void same_file(FILE *a, FILE *b)
{
    int x;
    int y;

    rewind(a);
    rewind(b);
    do {
        x = fgetc(a);
        y = fgetc(b);
        assert(x == y);
    } while (x != EOF);
    assert(!ferror(a) && !ferror(b));
}

/* cgrep -n JOBS alpha PATHS..., with the pool's lifetime counters checked. */
static int invoke(char **paths, int count, const char *jobs, FILE *out, FILE *err)
{
    char *args[16];
    int result;
    int before;
    int i;

    assert(count <= 11);
    args[0] = (char *)"cgrep";
    args[1] = (char *)"-n";
    args[2] = (char *)"--no-ignore";
    args[3] = (char *)jobs;
    args[4] = (char *)"alpha";
    for (i = 0; i < count; i++)
        args[5 + i] = paths[i];
    args[5 + count] = NULL;
    before = descriptors();
    allocations = 0;
    creates = 0;
    started_threads = 0;
    joined = 0;
    mutexes = 0;
    destroyed_mutexes = 0;
    conditions = 0;
    destroyed_conditions = 0;
    startup_attempts = 0;
    captured_pool = NULL;
    last_claims = 0;
    last_started = 0;
    subdir_opens = 0;
    cu_thread_fail_at(startup_failure);
    result = parallel_cli(5 + count, args, stdin, out, err);
    cu_thread_fail_at((size_t)-1);
    assert(descriptors() == before);
    assert(started_threads == joined);
    assert(mutexes == destroyed_mutexes);
    assert(conditions == destroyed_conditions);
    assert(captured_pool == NULL);
    return result;
}

static void write_fixture(const char *path, int large)
{
    FILE *file;
    int i;

    file = fopen(path, "wb");
    assert(file != NULL);
    if (large == 2) {
        assert(fputs("alpha", file) >= 0);
        for (i = 0; i < 2 * CHANNEL_BYTES; i++)
            assert(fputc('x', file) != EOF);
        assert(fputc('\n', file) != EOF);
    }
    /* Large fixtures emit more than two channel slots of numbered output. */
    for (i = 0; i < (large == 1 ? 2 * CHANNEL_BYTES / 17 + 1000 : 2); i++)
        assert(fputs("alpha beta gamma\n", file) >= 0);
    assert(fputs("alpha tail", file) >= 0);
    assert(fclose(file) == 0);
}

static void channel_fit_cases(void)
{
    ParallelWorker *worker;
    CuStr parts[5];
    size_t capacity;
    size_t room;

    worker = (ParallelWorker *)malloc(sizeof *worker);
    assert(worker != NULL);
    worker->pool = NULL;
    worker->fill = 0;
    worker->slots[0].state = SLOT_FREE;
    worker->bytes = worker->slots[0].bytes;
    capacity = CHANNEL_BYTES;
    parts[0] = cu_str_n(NULL, 0);
    parts[1] = cu_str_n("ab", 2);
    parts[2] = cu_str_n(":", 1);
    parts[3] = cu_str_n("\303\251\n", 3);
    parts[4] = cu_str_n(NULL, 0);
    for (room = 6; room <= 7; room++) {
        memset(worker->bytes, '#', capacity);
        worker->used = capacity - room;
        assert(channel_write(worker, parts, 5));
        assert(worker->used == capacity - room + 6);
        assert(memcmp(worker->bytes + capacity - room, "ab:\303\251\n", 6) == 0);
        assert(worker->bytes[capacity - room - 1] == '#');
        if (room == 7)
            assert(worker->bytes[capacity - 1] == '#');
        assert(worker->slots[0].state == SLOT_FREE && worker->fill == 0);
    }
    worker->used = capacity;
    assert(channel_write(worker, NULL, 0));
    assert(channel_write(worker, parts, 1));
    assert(worker->used == capacity && worker->slots[0].state == SLOT_FREE);
    free(worker);
}

/* Startup attempts for -j4: the mutex, the pool condition, then a
 * condition and a thread per worker as the four listed inputs start them. */
static void startup_cases(char **paths, FILE *expected, FILE *expected_err)
{
    FILE *out;
    FILE *err;
    FILE *wanted;
    size_t n;

    wanted = tmpfile();
    assert(wanted != NULL);
    assert(fprintf(wanted, "cgrep: worker startup: %s\n", strerror(EAGAIN)) > 0);
    for (n = 0; n <= 10; n++) {
        startup_failure = n;
        out = tmpfile();
        err = tmpfile();
        assert(out != NULL && err != NULL);
        assert(invoke(paths, 4, "-j4", out, err) == (n < 10 ? 2 : 0));
        assert(startup_attempts == (n < 10 ? n + 1 : 10));
        if (n < 2) {
            /* No scope: nothing searched. */
            rewind(out);
            assert(fgetc(out) == EOF);
            same_file(err, wanted);
            assert(started_threads == 0);
        } else if (n < 10) {
            /* A worker failed to start: reported, and the search goes on
             * with the workers that did start, or inline with none. */
            same_file(out, expected);
            assert(started_threads == (int)(n - 2) / 2);
            rewind(err);
            rewind(wanted);
            while (!feof(wanted)) {
                int c = fgetc(wanted);
                if (c == EOF)
                    break;
                assert(fgetc(err) == c);
            }
            rewind(expected_err);
            while (!feof(expected_err)) {
                int c = fgetc(expected_err);
                if (c == EOF)
                    break;
                assert(fgetc(err) == c);
            }
            assert(fgetc(err) == EOF);
        } else {
            assert(started_threads == 4);
            same_file(out, expected);
            same_file(err, expected_err);
        }
        assert(fclose(out) == 0 && fclose(err) == 0);
    }
    startup_failure = (size_t)-1;
    assert(fclose(wanted) == 0);

    /* An unavailable backend: serial before the scope, silently fewer
     * workers inside it. */
    for (n = 0; n < 10; n++) {
        unavailable_at = n;
        out = tmpfile();
        err = tmpfile();
        assert(out != NULL && err != NULL);
        assert(invoke(paths, 4, "-j4", out, err) == 0);
        assert(startup_attempts == n + 1);
        assert(started_threads == (n < 2 ? 0 : (int)(n - 2) / 2));
        same_file(out, expected);
        same_file(err, expected_err);
        assert(fclose(out) == 0 && fclose(err) == 0);
    }
    unavailable_at = (size_t)-1;
}

static void policy_cases(void)
{
    ParallelPool pool;

    /* Claims double while inputs stay small, and shrink to what fits. */
    assert(next_batch(1, 0, 1) == 2);
    assert(next_batch(2, 2000, 2) == 4);
    assert(next_batch(32, 64 * 1000, 32) == 64);
    assert(next_batch(64, 64 * 1000, 64) == 64);
    assert(next_batch(64, 64 * 1048576UL, 64) == 3);
    assert(next_batch(4, 4 * 1048576UL, 4) == 3);
    assert(next_batch(4, 4 * 4194304UL, 4) == 1);
    assert(next_batch(1, 4194304UL * 8, 1) == 1);
    assert(next_batch(8, 4194304UL, 8) == 7);
    /* A claim takes what is available, and at most a fair share once the
     * listing is complete. */
    pool.count = 10;
    pool.next = 0;
    pool.listed = 0;
    pool.started = 4;
    pool.jobs = 4;
    assert(claim_size(&pool, 64) == 10);
    assert(claim_size(&pool, 3) == 3);
    pool.listed = 1;
    assert(claim_size(&pool, 64) == 3);
    pool.next = 2;
    assert(claim_size(&pool, 64) == 2);
    pool.next = 9;
    assert(claim_size(&pool, 64) == 1);
    pool.started = 1;
    pool.next = 0;
    assert(claim_size(&pool, 64) == 10);
    pool.next = 10;
    assert(claim_size(&pool, 64) == 1);
    assert(wanted_workers(&pool) == 0);
    pool.next = 0;
    pool.jobs = 32;
    assert(wanted_workers(&pool) == 10);
    pool.jobs = 2;
    assert(wanted_workers(&pool) == 2);
}

/* Six files at -j2 with claims starting at three: once listed, a claim
 * is at most the unclaimed share per worker, so the batches are the first
 * three files, the next two and the last one. */
static void batch_cases(char **paths)
{
    ParallelWorker worker;
    ScanResult result;
    FILE *expected;
    FILE *expected_err;
    FILE *out;
    FILE *err;
    FILE *wanted;
    int i;

    for (i = 0; i < 6; i++)
        write_fixture(paths[i], i == 1 ? 1 : i == 4 ? 2 : 0);
    expected = tmpfile();
    expected_err = tmpfile();
    assert(expected != NULL && expected_err != NULL);
    assert(invoke(paths, 6, "-j1", expected, expected_err) == 0);
    test_batch_start = 3;
    out = tmpfile();
    err = tmpfile();
    assert(out != NULL && err != NULL);
    /* A large file in the first batch spills chunks; the small ones after
     * it are recorded in the batch's last slot. */
    assert(invoke(paths, 6, "-j2", out, err) == 0);
    assert(last_claims >= 2 && last_claims <= 3 && last_started == 2);
    same_file(out, expected);
    same_file(err, expected_err);
    assert(fclose(out) == 0 && fclose(err) == 0);
    /* A non-regular input in the middle of a batch is searched by the
     * coordinator at its position. */
    fault = F_SERIAL;
    fault_path = paths[1];
    out = tmpfile();
    err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(invoke(paths, 6, "-j2", out, err) == 0);
    assert(last_claims >= 2 && last_claims <= 3);
    same_file(out, expected);
    same_file(err, expected_err);
    assert(fclose(out) == 0 && fclose(err) == 0);
    /* A non-regular input without a match is searched serially and is
     * neither a match nor a failure. */
    out = fopen(paths[5], "wb");
    assert(out != NULL && fputs("beta only\n", out) >= 0 && fclose(out) == 0);
    out = tmpfile();
    err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(invoke(paths, 6, "-j1", out, err) == 0);
    assert(fclose(expected) == 0 && fclose(expected_err) == 0);
    expected = out;
    expected_err = err;
    fault = F_SERIAL;
    fault_path = paths[5];
    out = tmpfile();
    err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(invoke(paths, 6, "-j2", out, err) == 0);
    same_file(out, expected);
    same_file(err, expected_err);
    assert(fclose(out) == 0 && fclose(err) == 0);
    write_fixture(paths[5], 0);
    /* An unreadable input in a batch is reported at its position. */
    fault = F_OPEN;
    fault_path = paths[2];
    worker.path = paths[2];
    worker.walked = 0;
    assert(worker_scan(&worker, NULL, NULL, &result) == 0);
    assert(result.error == SCAN_OPEN && result.system_error == EACCES);
    assert(!result.matched && result.bytes == 0);
    assert(strcmp(scan_error(result), strerror(EACCES)) == 0);
    fault = F_FDOPEN;
    assert(worker_scan(&worker, NULL, NULL, &result) == 0);
    assert(result.error == SCAN_OPEN);
    fault = F_OPEN;
    out = tmpfile();
    err = tmpfile();
    wanted = tmpfile();
    assert(out != NULL && err != NULL && wanted != NULL);
    assert(invoke(paths, 6, "-j2", out, err) == 2);
    assert(fprintf(wanted, "cgrep: %s: %s\n", paths[2], strerror(EACCES)) > 0);
    same_file(err, wanted);
    rewind(out);
    assert(fgetc(out) != EOF);
    assert(fclose(out) == 0 && fclose(err) == 0 && fclose(wanted) == 0);
    fault = F_NONE;
    fault_path = NULL;
    /* Output fails inside the first batch: cancelled, joined, status 2. */
    out = fopen(paths[0], "rb");
    err = tmpfile();
    wanted = tmpfile();
    assert(out != NULL && err != NULL && wanted != NULL);
    alarm(15);
    assert(invoke(paths, 6, "-j2", out, err) == 2);
    alarm(0);
    assert(fputs("cgrep: output error\n", wanted) >= 0);
    same_file(err, wanted);
    assert(fclose(out) == 0 && fclose(err) == 0 && fclose(wanted) == 0);
    test_batch_start = 1;
    assert(fclose(expected) == 0 && fclose(expected_err) == 0);
}

/* A directory argument: listed on a walker thread while workers search. */
static void walk_cases(const char *directory)
{
    char root[160];
    char sub[192];
    char deep[224];
    char path[256];
    char storage[256];
    char *args[1];
    char *roots[2];
    FILE *expected;
    FILE *expected_err;
    FILE *out;
    FILE *err;
    FILE *wanted;
    int i;

    assert(sprintf(root, "%s/tree", directory) > 0);
    assert(sprintf(sub, "%s/sub", root) > 0);
    assert(sprintf(deep, "%s/deep", sub) > 0);
    assert(mkdir(root, 0700) == 0 && mkdir(sub, 0700) == 0 && mkdir(deep, 0700) == 0);
    /* 70 files before "sub" in byte order: the first publication of 64
     * happens while the walk continues. */
    for (i = 0; i < 70; i++) {
        assert(sprintf(path, "%s/a%02d", root, i) > 0);
        write_fixture(path, i == 7);
    }
    assert(sprintf(storage, "%s/a00", root) > 0);
    for (i = 0; i < 5; i++) {
        assert(sprintf(path, "%s/f%d", sub, i) > 0);
        write_fixture(path, 0);
        assert(sprintf(path, "%s/g%d", deep, i) > 0);
        write_fixture(path, 0);
    }
    args[0] = root;
    expected = tmpfile();
    expected_err = tmpfile();
    assert(expected != NULL && expected_err != NULL);
    assert(invoke(args, 1, "-j1", expected, expected_err) == 0);
    assert(creates == 0);
    rewind(expected_err);
    assert(fgetc(expected_err) == EOF);
    /* Listing "sub" waits until the first 64 inputs are claimed, so the
     * search ran before the walk finished; the output order still holds. */
    fault = F_WALK;
    fault_path = sub;
    out = tmpfile();
    err = tmpfile();
    assert(out != NULL && err != NULL);
    alarm(15);
    assert(invoke(args, 1, "-j4", out, err) == 0);
    alarm(0);
    assert(creates == 5 && last_started == 4 && last_claims >= 2);
    same_file(out, expected);
    same_file(err, expected_err);
    assert(fclose(out) == 0 && fclose(err) == 0);
    /* Without a memory stream for the walker's diagnostics, the
     * coordinator lists before searching. */
    fault = F_MEMSTREAM;
    fault_path = NULL;
    out = tmpfile();
    err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(invoke(args, 1, "-j4", out, err) == 0);
    assert(creates == 4);
    same_file(out, expected);
    same_file(err, expected_err);
    assert(fclose(out) == 0 && fclose(err) == 0);
    /* The walker thread fails to start: reported, and the coordinator lists
     * before searching. Unavailable: the same, silently. */
    fault = F_NONE;
    startup_failure = 2;
    out = tmpfile();
    err = tmpfile();
    wanted = tmpfile();
    assert(out != NULL && err != NULL && wanted != NULL);
    assert(invoke(args, 1, "-j4", out, err) == 2);
    assert(creates == 5 && started_threads == 4);
    assert(fprintf(wanted, "cgrep: worker startup: %s\n", strerror(EAGAIN)) > 0);
    same_file(out, expected);
    same_file(err, wanted);
    assert(fclose(out) == 0 && fclose(err) == 0 && fclose(wanted) == 0);
    startup_failure = (size_t)-1;
    unavailable_at = 2;
    out = tmpfile();
    err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(invoke(args, 1, "-j4", out, err) == 0);
    assert(creates == 5 && started_threads == 4);
    same_file(out, expected);
    same_file(err, expected_err);
    assert(fclose(out) == 0 && fclose(err) == 0);
    unavailable_at = (size_t)-1;
    /* Output fails on the first batch; the walker stops listing once it
     * notices the cancellation (here, when it enters "sub"), so the second
     * root is never listed. A diagnostic written before that (an unlistable
     * directory) still reaches err, after the output error. No input is
     * opened until the walk is listing "sub", so the output cannot fail
     * before the walk has passed "locked"; a slow walker (as under
     * valgrind) could otherwise see the cancellation first and rightly
     * list neither. */
    assert(sprintf(path, "%s/locked", root) > 0);
    assert(mkdir(path, 0700) == 0);
    if (geteuid() != 0)
        assert(chmod(path, 0) == 0);
    fault = F_STOP;
    fault_path = sub;
    race_ready = 0;
    assert(pthread_mutex_init(&race_mutex, NULL) == 0);
    assert(pthread_cond_init(&race_changed, NULL) == 0);
    count_root = root;
    roots[0] = root;
    roots[1] = root;
    out = fopen(storage, "rb");
    err = tmpfile();
    wanted = tmpfile();
    assert(out != NULL && err != NULL && wanted != NULL);
    alarm(15);
    assert(invoke(roots, 2, "-j4", out, err) == 2);
    alarm(0);
    assert(pthread_cond_destroy(&race_changed) == 0);
    assert(pthread_mutex_destroy(&race_mutex) == 0);
    assert(subdir_opens == 2);
    assert(fputs("cgrep: output error\n", wanted) >= 0);
    if (geteuid() != 0)
        assert(fprintf(wanted, "cgrep: %s: %s\n", path, strerror(EACCES)) > 0);
    same_file(err, wanted);
    assert(fclose(out) == 0 && fclose(err) == 0 && fclose(wanted) == 0);
    assert(chmod(path, 0700) == 0 && rmdir(path) == 0);
    count_root = NULL;
    fault = F_NONE;
    fault_path = NULL;
    /* The walker's diagnostics reach err through the coordinator. Two
     * unlistable directories, one before and one after the first
     * publication of 64 inputs, so the second copy starts where the first
     * ended. */
    if (geteuid() != 0) {
        FILE *serial_out;
        char early[224];

        assert(sprintf(early, "%s/a1x", root) > 0);
        assert(mkdir(early, 0) == 0);
        assert(chmod(deep, 0) == 0);
        serial_out = tmpfile();
        out = tmpfile();
        err = tmpfile();
        wanted = tmpfile();
        assert(serial_out != NULL && out != NULL && err != NULL && wanted != NULL);
        assert(invoke(args, 1, "-j1", serial_out, wanted) == 2);
        rewind(wanted);
        assert(fgetc(wanted) != EOF);
        assert(invoke(args, 1, "-j4", out, err) == 2);
        assert(creates == 5);
        same_file(out, serial_out);
        same_file(err, wanted);
        rewind(wanted);
        assert(fgetc(wanted) == 'c');
        assert(fclose(serial_out) == 0 && fclose(out) == 0 && fclose(err) == 0 && fclose(wanted) == 0);
        assert(chmod(deep, 0700) == 0);
        assert(rmdir(early) == 0);
    }
    assert(fclose(expected) == 0 && fclose(expected_err) == 0);
    for (i = 0; i < 5; i++) {
        assert(sprintf(path, "%s/f%d", sub, i) > 0);
        assert(unlink(path) == 0);
        assert(sprintf(path, "%s/g%d", deep, i) > 0);
        assert(unlink(path) == 0);
    }
    for (i = 0; i < 70; i++) {
        assert(sprintf(path, "%s/a%02d", root, i) > 0);
        assert(unlink(path) == 0);
    }
    assert(rmdir(deep) == 0 && rmdir(sub) == 0 && rmdir(root) == 0);
}

static void native_cases(void)
{
    char directory[] = "/tmp/cgrep-parallel-test.XXXXXX";
    char storage[6][128];
    char *paths[6];
    FILE *expected;
    FILE *expected_err;
    FILE *out;
    FILE *err;
    int i;
    int f;

    assert(mkdtemp(directory) != NULL);
    for (i = 0; i < 6; i++) {
        assert(sprintf(storage[i], "%s/f%d", directory, i) > 0);
        paths[i] = storage[i];
        write_fixture(paths[i], i == 0 || i == 2);
    }
    expected = tmpfile();
    expected_err = tmpfile();
    assert(expected != NULL && expected_err != NULL);
    assert(invoke(paths, 4, "-j1", expected, expected_err) == 0);
    assert(creates == 0);
    /* Every input starts a worker here, up to the job count. */
    for (i = 0; i < 3; i++) {
        out = tmpfile();
        err = tmpfile();
        assert(out != NULL && err != NULL);
        assert(invoke(paths, 4, i == 0 ? "-j2" : i == 1 ? "-j4" : "-j8", out, err) == 0);
        assert(creates == (i == 0 ? 2 : 4) && last_started == creates);
        assert(last_claims >= 2 && last_claims <= 4);
        same_file(out, expected);
        same_file(err, expected_err);
        assert(fclose(out) == 0 && fclose(err) == 0);
    }
    /* One body fragment exceeds two channels; it must use ordered fallback. */
    write_fixture(paths[0], 2);
    out = tmpfile();
    err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(invoke(paths, 4, "-j1", out, err) == 0);
    assert(fclose(expected) == 0 && fclose(expected_err) == 0);
    expected = out;
    expected_err = err;
    for (i = 0; i < 2; i++) {
        out = tmpfile();
        err = tmpfile();
        assert(out != NULL && err != NULL);
        assert(invoke(paths, 4, i == 0 ? "-j2" : "-j8", out, err) == 0);
        same_file(out, expected);
        same_file(err, expected_err);
        assert(fclose(out) == 0 && fclose(err) == 0);
    }
    startup_cases(paths, expected, expected_err);
    /* The name becomes a directory after open. Classification and scanning
     * must still use the original open file, never the replaced pathname. */
    fault = F_REPLACE;
    fault_path = paths[1];
    out = tmpfile();
    err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(invoke(paths, 4, "-j4", out, err) == 0);
    same_file(out, expected);
    same_file(err, expected_err);
    assert(fclose(out) == 0 && fclose(err) == 0);
    assert(rmdir(paths[1]) == 0);
    write_fixture(paths[1], 0);
    fault = F_NONE;
    for (f = F_POOL; f <= F_CLOSE; f++) {
        fault = f;
        fault_path = paths[1];
        out = tmpfile();
        err = tmpfile();
        assert(out != NULL && err != NULL);
        assert(invoke(paths, 4, "-j4", out, err) == 2);
        rewind(err);
        assert(fgetc(err) != EOF);
        if (f == F_STAT || f == F_READ || f == F_CLOSE) {
            FILE *wanted;
            int j;

            wanted = tmpfile();
            assert(wanted != NULL);
            for (j = 0; j < 4; j++)
                assert(fprintf(wanted, "cgrep: %s: %s\n", paths[j], strerror(EIO)) > 0);
            same_file(err, wanted);
            assert(fclose(wanted) == 0);
        }
        /* No pool, or no mutex: nothing searched. A worker that cannot be
         * allocated leaves the search to those that could. */
        if (f == F_POOL || f == F_ARENA) {
            rewind(out);
            assert(fgetc(out) == EOF);
        } else if (f == F_WORKERS) {
            same_file(out, expected);
        }
        if (f == F_OPEN) {
            rewind(out);
            assert(fgetc(out) != EOF);
        }
        assert(fclose(out) == 0 && fclose(err) == 0);
    }
    fault = F_SERIAL;
    out = tmpfile();
    err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(invoke(paths, 4, "-j4", out, err) == 0);
    same_file(out, expected);
    same_file(err, expected_err);
    assert(fclose(out) == 0 && fclose(err) == 0);
    /* The coordinator's serial search of a non-regular input can fail. */
    if (geteuid() != 0) {
        FILE *wanted;

        fault = F_SERIAL_FAIL;
        out = tmpfile();
        err = tmpfile();
        wanted = tmpfile();
        assert(out != NULL && err != NULL && wanted != NULL);
        assert(invoke(paths, 4, "-j4", out, err) == 2);
        assert(fprintf(wanted, "cgrep: %s: %s\n", paths[1], strerror(EACCES)) > 0);
        same_file(err, wanted);
        rewind(out);
        assert(fgetc(out) != EOF);
        assert(fclose(out) == 0 && fclose(err) == 0 && fclose(wanted) == 0);
        assert(chmod(paths[1], 0600) == 0);
    }
    /* Without a scope, the serial loop stops at an output failure, which
     * the input's own search reports first. */
    fault = F_NONE;
    out = fopen(paths[0], "rb");
    err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(invoke(paths, 4, "-j1", out, err) == 2);
    {
        FILE *wanted;

        wanted = tmpfile();
        assert(wanted != NULL);
        assert(fprintf(wanted, "cgrep: %s: output error\ncgrep: output error\n", paths[0]) > 0);
        same_file(err, wanted);
        assert(fclose(wanted) == 0);
    }
    assert(fclose(out) == 0 && fclose(err) == 0);
    /* The only worker fails to start, so the coordinator searches inline,
     * where the output fails. */
    startup_failure = 3;
    out = fopen(paths[0], "rb");
    err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(invoke(paths, 4, "-j4", out, err) == 2);
    assert(started_threads == 0);
    {
        FILE *wanted;

        wanted = tmpfile();
        assert(wanted != NULL);
        assert(fprintf(wanted, "cgrep: worker startup: %s\ncgrep: %s: output error\ncgrep: output error\n",
                       strerror(EAGAIN), paths[0]) > 0);
        same_file(err, wanted);
        assert(fclose(wanted) == 0);
    }
    assert(fclose(out) == 0 && fclose(err) == 0);
    startup_failure = (size_t)-1;

    /* A raced FIFO in a later slot must not trap cancellation or scope join. */
    fault = F_FIFO;
    race_ready = 0;
    assert(pthread_mutex_init(&race_mutex, NULL) == 0);
    assert(pthread_cond_init(&race_changed, NULL) == 0);
    out = fopen(paths[0], "rb");
    err = tmpfile();
    assert(out != NULL && err != NULL);
    alarm(15);
    assert(invoke(paths, 4, "-j4", out, err) == 2);
    alarm(0);
    fault = F_NONE;
    fault_path = NULL;
    assert(pthread_cond_destroy(&race_changed) == 0);
    assert(pthread_mutex_destroy(&race_mutex) == 0);
    assert(fclose(out) == 0 && fclose(err) == 0);
    assert(fclose(expected) == 0 && fclose(expected_err) == 0);
    /* The raced case left a FIFO behind. */
    assert(unlink(paths[1]) == 0);
    batch_cases(paths);
    walk_cases(directory);
    for (i = 0; i < 6; i++)
        assert(unlink(paths[i]) == 0);
    assert(rmdir(directory) == 0);
}
#endif

typedef struct {
    char bytes[192];
    size_t used;
    int calls;
    int batches;
    int fail;
} CallbackOutput;

static int callback_write(void *context, const CuStr *parts, size_t count)
{
    CallbackOutput *out;
    size_t i;

    out = (CallbackOutput *)context;
    out->batches++;
    assert(count <= 5);
    for (i = 0; i < count; i++) {
        out->calls++;
        if (out->calls == out->fail)
            return 0;
        assert(parts[i].len <= sizeof out->bytes - out->used);
        memcpy(out->bytes + out->used, parts[i].ptr, parts[i].len);
        out->used += parts[i].len;
    }
    return 1;
}

static void callback_cases(void)
{
    CuArena arena;
    Search search;
    CallbackOutput output;
    unsigned long lines;
    int i;
    const char *expected;

    cu_arena_init(&arena);
    assert(literal_init(&search.literal, &arena, cu_str_cstr("alpha")));
    search_init(&search, NULL);
    search.names = 1;
    search.numbers = 1;
    search.out = NULL;
    search.write = callback_write;
    search.cancelled = NULL;
    search.finder = NULL;
    search.context = &output;
    for (i = 0; i <= 5; i++) {
        output.used = 0;
        output.calls = 0;
        output.batches = 0;
        output.fail = i;
        lines = 0;
        assert(search_text(&search, "f", cu_str_cstr("xx\nalpha tail"), &lines) == (i == 0 ? 1 : -1));
        if (i == 0) {
            expected = "f:2:alpha tail\n";
            assert(output.used == strlen(expected));
            assert(memcmp(output.bytes, expected, output.used) == 0);
        } else {
            static const char *const partial[] = {"", "f", "f:", "f:2:", "f:2:alpha tail"};

            assert(output.calls == i);
            assert(output.used == strlen(partial[i - 1]));
            assert(memcmp(output.bytes, partial[i - 1], output.used) == 0);
        }
        assert(output.batches == 1);
    }
    for (i = 0; i <= 3; i++) {
        output.used = 0;
        output.calls = 0;
        output.batches = 0;
        output.fail = i;
        assert(binary_warning(&search, "f", 7) == (i == 0 ? 1 : -1));
        if (i != 0) {
            expected = i == 1 ? "" : i == 2 ? "f" : "f: ";
            assert(output.calls == i);
            assert(output.used == strlen(expected));
            assert(memcmp(output.bytes, expected, output.used) == 0);
        }
        assert(output.batches == 1);
    }
    search.names = 0;
    search.numbers = 0;
    output.used = 0;
    output.calls = 0;
    output.batches = 0;
    output.fail = 0;
    lines = 0;
    assert(search_text(&search, "f", cu_str_cstr("alpha"), &lines) == 1);
    assert(output.used == 6 && memcmp(output.bytes, "alpha\n", 6) == 0);
    assert(binary_warning(&search, "f", 7) == 1);
    cu_arena_release(&arena);
}

#define SCAN_IN "/tmp/cgrep-parallel-scan.in"
#define SCAN_OUT "/tmp/cgrep-parallel-scan.out"

static int always_cancelled(void *context)
{
    (void)context;
    return 1;
}

/* The scanner's error texts, and a counting scan with a fresh per-window
 * finder that is cancelled before its first refill. */
static void scanner_cases(void)
{
    CuArena arena;
    CuArena scratch_arena;
    CuVec buffer;
    CuVec scratch;
    Search search;
    CuInput input;
    ScanResult result;
    FILE *file;

    result.matched = 0;
    result.system_error = 0;
    result.bytes = 0;
    result.error = SCAN_LINE_LIMIT;
    assert(strcmp(scan_error(result), "line exceeds 8 MiB limit") == 0);
    result.error = SCAN_RANGE;
    assert(strcmp(scan_error(result), "input exceeds byte-offset range") == 0);
    result.error = SCAN_CANCELLED;
    assert(strcmp(scan_error(result), "cancelled") == 0);
    result.error = SCAN_MEMORY;
    assert(strcmp(scan_error(result), "out of memory") == 0);
    result.error = SCAN_OUTPUT;
    assert(strcmp(scan_error(result), "output error") == 0);
    result.error = SCAN_IO;
    assert(strcmp(scan_error(result), "input/output error") == 0);
    result.error = SCAN_OK;
    assert(strcmp(scan_error(result), "unknown error") == 0);

    cu_arena_init(&arena);
    CU_VEC_INIT(&buffer, &arena, char);
    assert(literal_init(&search.literal, &arena, cu_str_cstr("alpha")));
    search_init(&search, NULL);
    search.count = 1;
    search.finder = NULL;
    search.cancelled = always_cancelled;
    /* Named files: the portable build has no tmpfile. */
    file = fopen(SCAN_IN, "w+b");
    assert(file != NULL);
    assert(fputs("alpha\nbeta\nalpha\n", file) >= 0);
    rewind(file);
    cu_input_attach(&input, file);
    result = search_stream(&search, "f", &input, &buffer);
    assert(result.error == SCAN_CANCELLED && !result.matched && result.bytes == 0);
    assert(cu_input_close(&input));
    rewind(file);
    search.cancelled = NULL;
    search.out = fopen(SCAN_OUT, "w+b");
    assert(search.out != NULL);
    cu_input_attach(&input, file);
    result = search_stream(&search, "f", &input, &buffer);
    assert(result.error == SCAN_OK && result.matched && result.bytes == 17);
    assert(cu_input_close(&input));
    rewind(search.out);
    assert(fgetc(search.out) == '2' && fgetc(search.out) == '\n' && fgetc(search.out) == EOF);
    /* A scratch vector that failed earlier is replaced before the scan,
     * and a window larger than one chunk still refills by the chunk. */
    cu_arena_init(&scratch_arena);
    CU_VEC_INIT(&scratch, &scratch_arena, char);
    scratch.failed = 1;
    search.scratch = &scratch;
    assert(cu_vec_reserve(&buffer, 3 * STREAM_CHUNK));
    rewind(file);
    rewind(search.out);
    cu_input_attach(&input, file);
    result = search_stream(&search, "f", &input, &buffer);
    assert(result.error == SCAN_OK && result.matched && result.bytes == 17);
    assert(!cu_vec_failed(&scratch));
    assert(cu_input_close(&input));
    rewind(search.out);
    assert(fgetc(search.out) == '2' && fgetc(search.out) == '\n');
#ifdef CGREP_NATIVE_PARALLEL
    /* A read error during a counting scan. */
    rewind(file);
    cu_input_attach(&input, file);
    fault = F_READ;
    result = search_stream(&search, "f", &input, &buffer);
    fault = F_NONE;
    assert(result.error == SCAN_IO && result.system_error == EIO && !result.matched);
    assert(!cu_input_close(&input));
    /* Printing with -i folds a window into scratch; a failed growth there
     * is out of memory for that input. */
    search.count = 0;
    search.fold = 1;
    rewind(file);
    cu_input_attach(&input, file);
    reserve_calls = 0;
    reserve_fail_at = 2;
    result = search_stream(&search, "f", &input, &buffer);
    reserve_fail_at = 0;
    assert(result.error == SCAN_MEMORY && !result.matched);
    assert(cu_input_close(&input));
    search.fold = 0;
    search.count = 1;
#endif
    /* The count's summary line can fail to be written. */
    assert(fclose(search.out) == 0);
    search.out = fopen(SCAN_OUT, "rb");
    assert(search.out != NULL);
    rewind(file);
    cu_input_attach(&input, file);
    result = search_stream(&search, "f", &input, &buffer);
    assert(result.error == SCAN_OUTPUT && result.matched);
    assert(cu_input_close(&input));
    assert(fclose(search.out) == 0);
    /* -i on a binary input: the refill holding the NUL is folded before
     * the counting scan that decides whether to warn. */
    assert(fclose(file) == 0);
    file = fopen(SCAN_IN, "w+b");
    search.out = fopen(SCAN_OUT, "w+b");
    assert(file != NULL && search.out != NULL);
    assert(fputs("beta\nALPHA\n", file) >= 0 && fputc('\0', file) != EOF && fputs("x\n", file) >= 0);
    rewind(file);
    search.count = 0;
    search.fold = 1;
    cu_input_attach(&input, file);
    result = search_stream(&search, "f", &input, &buffer);
    assert(result.error == SCAN_OK && result.matched);
    assert(cu_input_close(&input));
    rewind(search.out);
    assert(fgetc(search.out) != EOF);
    cu_arena_release(&scratch_arena);
    assert(fclose(search.out) == 0);
    assert(fclose(file) == 0);
    assert(remove(SCAN_IN) == 0 && remove(SCAN_OUT) == 0);
    cu_arena_release(&arena);
}

static void portable_options(void)
{
    char *args[4];
    FILE *in;
    FILE *out;
    FILE *err;

    in = fopen("/tmp/cgrep-parallel-options.in", "w+b");
    out = fopen("/tmp/cgrep-parallel-options.out", "w+b");
    err = fopen("/tmp/cgrep-parallel-options.err", "w+b");
    assert(in != NULL && out != NULL && err != NULL);
    args[0] = (char *)"cgrep";
    args[1] = (char *)"-j";
    args[2] = NULL;
    assert(parallel_cli(2, args, in, out, err) == 2);
    args[1] = (char *)"-j33";
    assert(parallel_cli(2, args, in, out, err) == 2);
    args[1] = (char *)"-j";
    args[2] = (char *)"4";
    args[3] = NULL;
    assert(parallel_cli(3, args, in, out, err) == 2);
    args[1] = (char *)"-j32";
    args[2] = (char *)"alpha";
    assert(fputs("alpha\n", in) >= 0);
    rewind(in);
    assert(parallel_cli(3, args, in, out, err) == 0);
    /* Long options and help, through the same parser. */
    args[1] = (char *)"--hidden";
    rewind(in);
    assert(parallel_cli(3, args, in, out, err) == 0);
    args[1] = (char *)"--null";
    rewind(in);
    assert(parallel_cli(3, args, in, out, err) == 0);
    args[1] = (char *)"-0n";
    rewind(in);
    assert(parallel_cli(3, args, in, out, err) == 0);
    args[1] = (char *)"-nh";
    assert(parallel_cli(2, args, in, out, err) == 0);
    rewind(out);
    assert(fgetc(out) != EOF);
    assert(fclose(in) == 0 && fclose(out) == 0 && fclose(err) == 0);
    assert(remove("/tmp/cgrep-parallel-options.in") == 0);
    assert(remove("/tmp/cgrep-parallel-options.out") == 0);
    assert(remove("/tmp/cgrep-parallel-options.err") == 0);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    assert(worker_count("1") == 1);
    assert(worker_count("8") == 8);
    assert(worker_count("33") == 0);
    assert(worker_count("0004") == 4);
    assert(worker_count("") == 0);
    assert(worker_count("0") == 0);
    assert(worker_count("32") == 32);
    assert(worker_count("99999999999999999999999") == 0);
    assert(worker_count("-1") == 0);
    assert(worker_count("1x") == 0);
    portable_options();
    callback_cases();
    scanner_cases();
#ifdef CGREP_NATIVE_PARALLEL
    channel_fit_cases();
    policy_cases();
    native_cases();
    puts("test_parallel: native PASS");
#else
    if (argc > 1)
        return 2;
    puts("test_parallel: portable parsing PASS; native backend unavailable");
#endif
    return 0;
}
