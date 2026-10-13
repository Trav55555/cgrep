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

enum { F_NONE, F_POOL, F_MUTEX, F_COND, F_ATTR, F_STACK, F_CREATE, F_OPEN,
       F_STAT, F_FDOPEN, F_WINDOW, F_READ, F_CLOSE, F_SERIAL, F_FIFO };
static int fault;
static int creates;
static int started_threads;
static int joined;
static int conditions;
static const char *fault_path;
static pthread_mutex_t race_mutex;
static pthread_cond_t race_changed;
static int race_ready;

static void *test_alloc(size_t n)
{
    return fault == F_POOL ? NULL : malloc(n);
}

static int test_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a)
{
    return fault == F_MUTEX ? EAGAIN : pthread_mutex_init(m, a);
}

static int test_cond_init(pthread_cond_t *c, const pthread_condattr_t *a)
{
    conditions++;
    return fault == F_COND && conditions == 2 ? EAGAIN : pthread_cond_init(c, a);
}

static int test_attr_init(pthread_attr_t *a)
{
    return fault == F_ATTR ? EAGAIN : pthread_attr_init(a);
}

static int test_stack(pthread_attr_t *a, size_t n)
{
    return fault == F_STACK ? EINVAL : pthread_attr_setstacksize(a, n);
}

static int test_create(pthread_t *t, const pthread_attr_t *a, void *(*fn)(void *), void *context)
{
    int r;

    creates++;
    if (fault == F_CREATE && creates == 2)
        return EAGAIN;
    r = pthread_create(t, a, fn, context);
    if (r == 0)
        started_threads++;
    return r;
}

static int test_join(pthread_t thread, void **result)
{
    int r;

    r = pthread_join(thread, result);
    if (r == 0)
        joined++;
    return r;
}

static int test_open(const char *path, int flags)
{
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
    return open(path, flags);
}

static int test_stat(int fd, struct stat *st)
{
    if (fault == F_STAT) {
        errno = EIO;
        return -1;
    }
    return fstat(fd, st);
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

#define CGREP_POOL_ALLOC test_alloc
#define CGREP_THREAD_CREATE test_create
#define pthread_join test_join
#define pthread_mutex_init test_mutex_init
#define pthread_cond_init test_cond_init
#define pthread_attr_init test_attr_init
#define pthread_attr_setstacksize test_stack
#define open test_open
#define fstat test_stat
#define fdopen test_fdopen
#define cu_vec_reserve test_reserve
#define cu_input_read test_read
#define fclose test_close
#endif
#define cgrep_main parallel_cli
#include "../src/cgrep.c"
#undef cgrep_main
#ifdef CGREP_NATIVE_PARALLEL
#undef pthread_join
#undef pthread_mutex_init
#undef pthread_cond_init
#undef pthread_attr_init
#undef pthread_attr_setstacksize
#undef open
#undef fstat
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
    creates = 0;
    started_threads = 0;
    joined = 0;
    conditions = 0;
    result = parallel_cli(8, args, stdin, out, err);
    assert(descriptors() == before);
    assert(started_threads == joined);
    return result;
}

static void write_fixture(const char *path, int large)
{
    FILE *file;
    int i;

    file = fopen(path, "wb");
    assert(file != NULL);
    for (i = 0; i < (large ? 14000 : 2); i++)
        assert(fputs("alpha beta gamma\n", file) >= 0);
    assert(fputs("alpha tail", file) >= 0);
    assert(fclose(file) == 0);
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
    for (f = F_POOL; f <= F_CLOSE; f++) {
        fault = f;
        fault_path = paths[1];
        out = tmpfile();
        err = tmpfile();
        assert(out != NULL && err != NULL);
        assert(invoke(paths, "-j4", out, err) == 2);
        rewind(err);
        assert(fgetc(err) != EOF);
        if (f == F_READ || f == F_CLOSE) {
            FILE *wanted;
            int j;

            wanted = tmpfile();
            assert(wanted != NULL);
            for (j = 0; j < 4; j++)
                assert(fprintf(wanted, "cgrep: %s: %s\n", paths[j], strerror(EIO)) > 0);
            same_file(err, wanted);
            assert(fclose(wanted) == 0);
        }
        if (f <= F_CREATE) {
            rewind(out);
            assert(fgetc(out) == EOF);
        }
        if (f == F_CREATE)
            assert(creates == 2);
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
    int fail;
} CallbackOutput;

static int callback_write(void *context, const char *bytes, size_t n)
{
    CallbackOutput *out;

    out = (CallbackOutput *)context;
    out->calls++;
    if (out->calls == out->fail)
        return 0;
    assert(n <= sizeof out->bytes - out->used);
    memcpy(out->bytes + out->used, bytes, n);
    out->used += n;
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
    search.names = 1;
    search.numbers = 1;
    search.out = NULL;
    search.write = callback_write;
    search.cancelled = NULL;
    search.context = &output;
    for (i = 0; i <= 5; i++) {
        output.used = 0;
        output.calls = 0;
        output.fail = i;
        lines = 0;
        assert(search_text(&search, "f", cu_str_cstr("xx\nalpha tail"), &lines) == (i == 0 ? 1 : -1));
        if (i == 0) {
            expected = "f:2:alpha tail\n";
            assert(output.used == strlen(expected));
            assert(memcmp(output.bytes, expected, output.used) == 0);
        } else {
            assert(output.calls == i);
        }
    }
    for (i = 0; i <= 3; i++) {
        output.used = 0;
        output.calls = 0;
        output.fail = i;
        assert(binary_warning(&search, "f", 7) == (i == 0 ? 1 : -1));
        if (i != 0)
            assert(output.calls == i);
    }
    search.names = 0;
    search.numbers = 0;
    output.used = 0;
    output.calls = 0;
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
    args[1] = (char *)"-j9";
    assert(parallel_cli(2, args, in, out, err) == 2);
    args[1] = (char *)"-j";
    args[2] = (char *)"4";
    args[3] = NULL;
    assert(parallel_cli(3, args, in, out, err) == 2);
    args[1] = (char *)"-j8";
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
    assert(worker_count("0004") == 4);
    assert(worker_count("") == 0);
    assert(worker_count("0") == 0);
    assert(worker_count("9") == 0);
    assert(worker_count("99999999999999999999999") == 0);
    assert(worker_count("-1") == 0);
    assert(worker_count("1x") == 0);
    portable_options();
    callback_cases();
#ifdef CGREP_NATIVE_PARALLEL
    native_cases();
    puts("test_parallel: native PASS");
#else
    if (argc > 1)
        return 2;
    puts("test_parallel: portable parsing PASS; native backend unavailable");
#endif
    return 0;
}
