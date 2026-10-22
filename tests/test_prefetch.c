#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include "cu.h"

#if defined(__linux__) && defined(__GLIBC__) && defined(_GNU_SOURCE) && !defined(__cerb__)
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

/* Listing ahead (parallel.inc): every threaded walk must report exactly
 * what -j1 reports, in the same order, whichever thread listed each
 * directory and whatever failed while setting the prefetcher up. */

static pthread_t main_thread;
static pthread_mutex_t counts = PTHREAD_MUTEX_INITIALIZER;
static int prefetch_starts;  /* prefetch thread start attempts */
static int prefetch_fail_at; /* attempt that fails, 0 for none */
static int walker_inits;     /* mutex/condition inits on the walker thread */
static int walker_init_fail; /* init that fails, 0 for none */
static int helper_lists;     /* directories listed by prefetch threads */
static pthread_t walker_thread;
static int walker_known;
static int threads_started;
static int joined;
static int mutexes;
static int conditions;
static const char *denied; /* opening this path fails with EACCES */
static const char *slow;   /* a prefetch thread lists this path slowly */
static const char *slow_skipped; /* and this ignored one, listed ahead */

static void prefetch_main(void *context);

static int test_create(CuThread **t, CuArena *a, size_t stack_size, void (*fn)(void *), void *context)
{
    int fail;
    int ok;

    fail = 0;
    if (fn == prefetch_main) {
        assert(pthread_mutex_lock(&counts) == 0);
        fail = ++prefetch_starts == prefetch_fail_at;
        assert(pthread_mutex_unlock(&counts) == 0);
    }
    if (fail) {
        errno = EAGAIN;
        return 0;
    }
    ok = cu_thread_start(t, a, stack_size, fn, context);
    if (ok) {
        assert(pthread_mutex_lock(&counts) == 0);
        threads_started++;
        assert(pthread_mutex_unlock(&counts) == 0);
    }
    return ok;
}

static void test_join(CuThread *t)
{
    cu_thread_join(t);
    assert(pthread_mutex_lock(&counts) == 0);
    joined++;
    assert(pthread_mutex_unlock(&counts) == 0);
}

/* Only the walker thread sets up a prefetcher; the coordinator is the
 * test's own thread and workers create nothing. */
static int walker_init_fails(void)
{
    int fail;

    if (pthread_equal(pthread_self(), main_thread))
        return 0;
    assert(pthread_mutex_lock(&counts) == 0);
    fail = ++walker_inits == walker_init_fail;
    assert(pthread_mutex_unlock(&counts) == 0);
    return fail;
}

static int test_mutex_init(CuMutex **m, CuArena *a)
{
    if (walker_init_fails()) {
        errno = EAGAIN;
        return 0;
    }
    if (!cu_mutex_init(m, a))
        return 0;
    assert(pthread_mutex_lock(&counts) == 0);
    mutexes++;
    assert(pthread_mutex_unlock(&counts) == 0);
    return 1;
}

static void test_mutex_destroy(CuMutex *m)
{
    cu_mutex_destroy(m);
    assert(pthread_mutex_lock(&counts) == 0);
    mutexes--;
    assert(pthread_mutex_unlock(&counts) == 0);
}

static int test_cond_init(CuCond **c, CuArena *a)
{
    if (walker_init_fails()) {
        errno = EAGAIN;
        return 0;
    }
    if (!cu_cond_init(c, a))
        return 0;
    assert(pthread_mutex_lock(&counts) == 0);
    conditions++;
    assert(pthread_mutex_unlock(&counts) == 0);
    return 1;
}

static void test_cond_destroy(CuCond *c)
{
    cu_cond_destroy(c);
    assert(pthread_mutex_lock(&counts) == 0);
    conditions--;
    assert(pthread_mutex_unlock(&counts) == 0);
}

/* The first listing off the coordinator is the root's, on the walker
 * thread; later listings on other threads are a prefetch thread's. */
static int test_dir_open(CuDir *d, CuArena *a, const char *path)
{
    int helper;

    helper = 0;
    if (!pthread_equal(pthread_self(), main_thread)) {
        assert(pthread_mutex_lock(&counts) == 0);
        if (!walker_known) {
            walker_thread = pthread_self();
            walker_known = 1;
        }
        helper = !pthread_equal(pthread_self(), walker_thread);
        helper_lists += helper;
        assert(pthread_mutex_unlock(&counts) == 0);
    }
    if (helper && ((slow != NULL && strcmp(path, slow) == 0) ||
                   (slow_skipped != NULL && strcmp(path, slow_skipped) == 0)))
        usleep(50000);
    if (denied != NULL && strcmp(path, denied) == 0) {
        d->handle = NULL;
        d->arena = a;
        d->state = -1;
        d->error = EACCES;
        return 0;
    }
    return cu_dir_open(d, a, path);
}

/* Every unclaimed input starts a worker. */
#define CGREP_START_BACKLOG 1
#define cu_thread_start test_create
#define cu_thread_join test_join
#define cu_mutex_init test_mutex_init
#define cu_mutex_destroy test_mutex_destroy
#define cu_cond_init test_cond_init
#define cu_cond_destroy test_cond_destroy
#define cu_dir_open test_dir_open
#define cgrep_main prefetch_cli
#include "../src/cgrep.c"
#undef cgrep_main
#undef cu_thread_start
#undef cu_thread_join
#undef cu_mutex_init
#undef cu_mutex_destroy
#undef cu_cond_init
#undef cu_cond_destroy
#undef cu_dir_open

static char base[] = "/tmp/cgrep-prefetch-XXXXXX";
static char root[64];

static void put(const char *rel, const char *text)
{
    char path[256];
    FILE *f;

    assert(sprintf(path, "%s/%s", root, rel) > 0);
    f = fopen(path, "wb");
    assert(f != NULL);
    assert(fputs(text, f) >= 0);
    assert(fclose(f) == 0);
}

static void dir(const char *rel)
{
    char path[256];

    assert(sprintf(path, "%s/%s", root, rel) > 0);
    assert(mkdir(path, 0700) == 0);
}

/* Nested directories, siblings past PREFETCH_AHEAD, ignore files, hidden
 * names and a directory that cannot be opened. */
static void build(void)
{
    char rel[64];
    int i;
    int j;

    assert(mkdtemp(base) != NULL);
    assert(sprintf(root, "%s/t", base) > 0);
    assert(mkdir(root, 0700) == 0);
    put(".ignore", "skipped/\n*.log\n");
    for (i = 0; i < 4; i++) {
        assert(sprintf(rel, "n%d", i) > 0);
        dir(rel);
        for (j = 0; j < 4; j++) {
            assert(sprintf(rel, "n%d/m%d", i, j) > 0);
            dir(rel);
            assert(sprintf(rel, "n%d/m%d/deep", i, j) > 0);
            dir(rel);
            assert(sprintf(rel, "n%d/m%d/deep/f.txt", i, j) > 0);
            put(rel, "alpha deep\n");
            assert(sprintf(rel, "n%d/m%d/f.txt", i, j) > 0);
            put(rel, j % 2 ? "alpha\n" : "beta\n");
            assert(sprintf(rel, "n%d/m%d/x.log", i, j) > 0);
            put(rel, "alpha ignored\n");
            /* Hidden names below the root are filtered from listings a
             * prefetch thread makes. */
            assert(sprintf(rel, "n%d/m%d/.dot", i, j) > 0);
            put(rel, "alpha dot\n");
            assert(sprintf(rel, "n%d/m%d/.dotdir", i, j) > 0);
            dir(rel);
            assert(sprintf(rel, "n%d/m%d/.dotdir/f", i, j) > 0);
            put(rel, "alpha dotdir\n");
            /* Ignored below the root: listed ahead before the walker's
             * verdict, then discarded with what was queued below it. */
            assert(sprintf(rel, "n%d/m%d/skipped", i, j) > 0);
            dir(rel);
            assert(sprintf(rel, "n%d/m%d/skipped/s", i, j) > 0);
            dir(rel);
            assert(sprintf(rel, "n%d/m%d/skipped/s/t", i, j) > 0);
            dir(rel);
            assert(sprintf(rel, "n%d/m%d/skipped/s/t/f", i, j) > 0);
            put(rel, "alpha skipped deep\n");
        }
    }
    dir("wide");
    for (i = 0; i < 300; i++) {
        assert(sprintf(rel, "wide/d%03d", i) > 0);
        dir(rel);
        assert(sprintf(rel, "wide/d%03d/f", i) > 0);
        put(rel, i % 7 ? "beta\n" : "alpha\n");
    }
    dir("skipped");
    put("skipped/f", "alpha skipped\n");
    dir(".hidden");
    put(".hidden/f", "alpha hidden\n");
    dir("locked");
    put("locked/f", "alpha locked\n");
    dir("slow");
    put("slow/f", "alpha slow\n");
    put("top", "alpha top\n");
}

typedef struct {
    int status;
    CuVec out;
    CuVec err;
} Run;

static void slurp(FILE *f, CuVec *into)
{
    int c;

    rewind(f);
    while ((c = fgetc(f)) != EOF) {
        char b;

        b = (char)c;
        CU_VEC_PUSH(into, &b);
    }
    assert(!cu_vec_failed(into));
    assert(fclose(f) == 0);
}

/* cgrep JOBS [FLAG] alpha ROOT, with counts reset and checked balanced. */
static void run(CuArena *a, const char *jobs, const char *flag, FILE *out, Run *r)
{
    char *argv[8];
    FILE *in;
    FILE *err;
    int argc;

    argc = 0;
    argv[argc++] = (char *)"cgrep";
    argv[argc++] = (char *)jobs;
    if (flag != NULL)
        argv[argc++] = (char *)flag;
    argv[argc++] = (char *)"alpha";
    argv[argc++] = root;
    argv[argc] = NULL;
    threads_started = joined = mutexes = conditions = 0;
    prefetch_starts = walker_inits = helper_lists = 0;
    walker_known = 0;
    in = tmpfile();
    err = tmpfile();
    assert(in != NULL && err != NULL);
    if (out == NULL)
        out = tmpfile();
    assert(out != NULL);
    r->status = prefetch_cli(argc, argv, in, out, err);
    assert(fclose(in) == 0);
    CU_VEC_INIT(&r->out, a, char);
    CU_VEC_INIT(&r->err, a, char);
    slurp(out, &r->out);
    slurp(err, &r->err);
    assert(threads_started == joined && mutexes == 0 && conditions == 0);
}

static void same(const Run *x, const Run *y)
{
    assert(x->status == y->status);
    assert(cu_vec_len(&x->out) == cu_vec_len(&y->out));
    assert(memcmp(x->out.data, y->out.data, cu_vec_len(&x->out)) == 0);
    assert(cu_vec_len(&x->err) == cu_vec_len(&y->err));
    assert(memcmp(x->err.data, y->err.data, cu_vec_len(&x->err)) == 0);
}

static void cases(void)
{
    static const char *const flags[] = {NULL, "--hidden", "--no-ignore", "--max-depth=2"};
    static const char *const jobs[] = {"-j2", "-j4", "-j8"};
    char locked[96];
    char slowed[96];
    char slowed_skipped[96];
    CuArena a;
    Run serial;
    Run threaded;
    size_t f;
    size_t j;
    int k;

    cu_arena_init(&a);
    assert(sprintf(locked, "%s/locked", root) > 0);
    assert(sprintf(slowed, "%s/slow", root) > 0);
    assert(sprintf(slowed_skipped, "%s/n0/m1/skipped", root) > 0);
    denied = locked;
    slow = slowed;
    slow_skipped = slowed_skipped;
    for (f = 0; f < sizeof flags / sizeof flags[0]; f++) {
        run(&a, "-j1", flags[f], NULL, &serial);
        assert(prefetch_starts == 0);
        assert(serial.status == 2 && cu_vec_len(&serial.err) != 0);
        for (j = 0; j < sizeof jobs / sizeof jobs[0]; j++) {
            run(&a, jobs[j], flags[f], NULL, &threaded);
            same(&threaded, &serial);
            /* Directories were listed ahead; a slow helper listing can
             * make the walker wait for one in flight. */
            assert(prefetch_starts >= 2 && helper_lists != 0);
        }
    }
    /* Failed setup lists in the walker alone, or with fewer threads. */
    run(&a, "-j1", NULL, NULL, &serial);
    for (k = 1; k <= 3; k++) {
        prefetch_fail_at = k;
        run(&a, "-j4", NULL, NULL, &threaded);
        same(&threaded, &serial);
        /* Starting stops at the first failure. */
        assert(prefetch_starts == k);
        if (k == 1)
            assert(helper_lists == 0);
    }
    prefetch_fail_at = 0;
    for (k = 1; k <= 3; k++) {
        walker_init_fail = k;
        run(&a, "-j4", NULL, NULL, &threaded);
        same(&threaded, &serial);
        assert(prefetch_starts == 0 && helper_lists == 0);
    }
    walker_init_fail = 0;
    /* An output failure cancels the walk with listings still queued or in
     * flight: all are joined and released (leaks show under sanitizers). */
    {
        FILE *full;

        full = fopen("/dev/full", "w");
        if (full != NULL) {
            run(&a, "-j4", NULL, full, &threaded);
            assert(threaded.status == 2);
        }
    }
    cu_arena_release(&a);
}

static void cleanup(void)
{
    char command[160];

    assert(sprintf(command, "rm -r -- '%s'", base) > 0);
    assert(system(command) == 0);
}

int main(void)
{
    main_thread = pthread_self();
    build();
    cases();
    cleanup();
    puts("test_prefetch: PASS");
    return 0;
}
#else
int main(void)
{
    puts("test_prefetch: native backend unavailable");
    return 0;
}
#endif
