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
       F_WINDOW, F_READ, F_CLOSE, F_SERIAL, F_FIFO, F_REPLACE };
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

static void *test_alloc(size_t n)
{
    allocations++;
    if (fault == F_POOL || (fault == F_WORKERS && allocations == 2))
        return NULL;
    return malloc(n);
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
    assert(stack_size == (size_t)262144);
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

    if (fault == F_FIFO && strcmp(path, fault_path) != 0) {
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

static int test_reserve(CuVec *buffer, size_t extra)
{
    if (fault == F_WINDOW) {
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
#define CGREP_POOL_ALLOC test_alloc
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

static int invoke(char **paths, const char *jobs, FILE *out, FILE *err)
{
    char *args[9];
    int result;
    int before;

    args[0] = (char *)"cgrep";
    args[1] = (char *)"-n";
    args[2] = (char *)jobs;
    args[3] = (char *)"alpha";
    args[4] = paths[0];
    args[5] = paths[1];
    args[6] = paths[2];
    args[7] = paths[3];
    args[8] = NULL;
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
    cu_thread_fail_at(startup_failure);
    result = parallel_cli(8, args, stdin, out, err);
    cu_thread_fail_at((size_t)-1);
    assert(descriptors() == before);
    assert(started_threads == joined);
    assert(mutexes == destroyed_mutexes);
    assert(conditions == destroyed_conditions);
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

static void startup_cases(char **paths, FILE *expected, FILE *expected_err)
{
    FILE *out;
    FILE *err;
    FILE *wanted;
    size_t n;

    wanted = tmpfile();
    assert(wanted != NULL);
    assert(fprintf(wanted, "cgrep: worker startup: %s\n", strerror(EAGAIN)) > 0);
    for (n = 0; n <= 9; n++) {
        startup_failure = n;
        out = tmpfile();
        err = tmpfile();
        assert(out != NULL && err != NULL);
        assert(invoke(paths, "-j4", out, err) == (n < 9 ? 2 : 0));
        assert(startup_attempts == (n < 9 ? n + 1 : 9));
        if (n < 9) {
            rewind(out);
            assert(fgetc(out) == EOF);
            same_file(err, wanted);
            assert(started_threads == (n < 5 ? 0 : (int)(n - 5)));
        } else {
            assert(started_threads == 4);
            same_file(out, expected);
            same_file(err, expected_err);
        }
        assert(fclose(out) == 0 && fclose(err) == 0);
    }
    startup_failure = (size_t)-1;
    assert(fclose(wanted) == 0);

    for (n = 0; n < 9; n++) {
        unavailable_at = n;
        out = tmpfile();
        err = tmpfile();
        assert(out != NULL && err != NULL);
        assert(invoke(paths, "-j4", out, err) == 0);
        assert(startup_attempts == n + 1);
        assert(started_threads == (n < 5 ? 0 : (int)(n - 5)));
        same_file(out, expected);
        same_file(err, expected_err);
        assert(fclose(out) == 0 && fclose(err) == 0);
    }
    unavailable_at = (size_t)-1;
}

static void native_cases(void)
{
    char directory[] = "/tmp/cgrep-parallel-test.XXXXXX";
    char storage[4][128];
    char *paths[4];
    FILE *expected;
    FILE *expected_err;
    FILE *out;
    FILE *err;
    int i;
    int f;

    assert(mkdtemp(directory) != NULL);
    for (i = 0; i < 4; i++) {
        assert(sprintf(storage[i], "%s/f%d", directory, i) > 0);
        paths[i] = storage[i];
        write_fixture(paths[i], i == 0 || i == 2);
    }
    expected = tmpfile();
    expected_err = tmpfile();
    assert(expected != NULL && expected_err != NULL);
    assert(invoke(paths, "-j1", expected, expected_err) == 0);
    assert(creates == 0);
    for (i = 0; i < 3; i++) {
        out = tmpfile();
        err = tmpfile();
        assert(out != NULL && err != NULL);
        assert(invoke(paths, i == 0 ? "-j2" : i == 1 ? "-j4" : "-j8", out, err) == 0);
        assert(creates == (i == 0 ? 2 : 4));
        same_file(out, expected);
        same_file(err, expected_err);
        assert(fclose(out) == 0 && fclose(err) == 0);
    }
    /* One body fragment exceeds two channels; it must use ordered fallback. */
    write_fixture(paths[0], 2);
    out = tmpfile();
    err = tmpfile();
    assert(out != NULL && err != NULL);
    assert(invoke(paths, "-j1", out, err) == 0);
    assert(fclose(expected) == 0 && fclose(expected_err) == 0);
    expected = out;
    expected_err = err;
    for (i = 0; i < 2; i++) {
        out = tmpfile();
        err = tmpfile();
        assert(out != NULL && err != NULL);
        assert(invoke(paths, i == 0 ? "-j2" : "-j8", out, err) == 0);
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
    assert(invoke(paths, "-j4", out, err) == 0);
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
        assert(invoke(paths, "-j4", out, err) == 2);
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
        if (f <= F_ARENA) {
            rewind(out);
            assert(fgetc(out) == EOF);
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
    assert(invoke(paths, "-j4", out, err) == 0);
    same_file(out, expected);
    same_file(err, expected_err);
    assert(fclose(out) == 0 && fclose(err) == 0);

    /* A raced FIFO in a later slot must not trap cancellation or scope join. */
    fault = F_FIFO;
    race_ready = 0;
    assert(pthread_mutex_init(&race_mutex, NULL) == 0);
    assert(pthread_cond_init(&race_changed, NULL) == 0);
    out = fopen(paths[0], "rb");
    err = tmpfile();
    assert(out != NULL && err != NULL);
    alarm(15);
    assert(invoke(paths, "-j4", out, err) == 2);
    alarm(0);
    fault = F_NONE;
    assert(pthread_cond_destroy(&race_changed) == 0);
    assert(pthread_mutex_destroy(&race_mutex) == 0);
    assert(fclose(out) == 0 && fclose(err) == 0);
    assert(fclose(expected) == 0 && fclose(expected_err) == 0);
    for (i = 0; i < 4; i++)
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
#ifdef CGREP_NATIVE_PARALLEL
    channel_fit_cases();
    native_cases();
    puts("test_parallel: native PASS");
#else
    if (argc > 1)
        return 2;
    puts("test_parallel: portable parsing PASS; native backend unavailable");
#endif
    return 0;
}
