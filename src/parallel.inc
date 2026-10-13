/* Native ordered worker scope. Included after the platform-independent scanner. */
#if defined(__linux__) && defined(__GLIBC__) && defined(_GNU_SOURCE) && !defined(__cerb__)
#if __GLIBC_PREREQ(2, 34)
#define CGREP_NATIVE_PARALLEL 1
#endif
#endif

#ifdef CGREP_NATIVE_PARALLEL
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef CGREP_POOL_ALLOC
#define CGREP_POOL_ALLOC malloc
#endif
#ifndef CGREP_THREAD_CREATE
#define CGREP_THREAD_CREATE pthread_create
#endif

#define PARALLEL_WORKERS 8
#define CHANNEL_BYTES 65536

typedef enum { JOB_IDLE, JOB_ASSIGNED, JOB_RUNNING, JOB_CHUNK, JOB_DONE, JOB_SERIAL } JobState;
typedef struct ParallelPool ParallelPool;
typedef struct {
    ParallelPool *pool;
    pthread_t thread;
    pthread_cond_t changed;
    const char *path;
    JobState state;
    ScanResult result;
    size_t used;
    char bytes[CHANNEL_BYTES];
} ParallelWorker;

struct ParallelPool {
    pthread_mutex_t mutex;
    int cancel;
    const Search *search;
    ParallelWorker workers[PARALLEL_WORKERS];
};

static void pool_lock(ParallelPool *pool)
{
    if (pthread_mutex_lock(&pool->mutex) != 0)
        abort();
}

static void pool_unlock(ParallelPool *pool)
{
    if (pthread_mutex_unlock(&pool->mutex) != 0)
        abort();
}

static void worker_wait(ParallelWorker *worker)
{
    if (pthread_cond_wait(&worker->changed, &worker->pool->mutex) != 0)
        abort();
}

static void worker_signal(ParallelWorker *worker)
{
    if (pthread_cond_signal(&worker->changed) != 0)
        abort();
}

static int worker_cancelled(void *context)
{
    ParallelWorker *worker;
    int cancel;

    worker = (ParallelWorker *)context;
    pool_lock(worker->pool);
    cancel = worker->pool->cancel;
    pool_unlock(worker->pool);
    return cancel;
}

/* Publication transfers the fixed channel to the coordinator until ack. */
static int worker_publish(ParallelWorker *worker)
{
    ParallelPool *pool;
    int ok;

    pool = worker->pool;
    pool_lock(pool);
    if (!pool->cancel) {
        worker->state = JOB_CHUNK;
        worker_signal(worker);
        while (worker->state == JOB_CHUNK && !pool->cancel)
            worker_wait(worker);
    }
    ok = !pool->cancel;
    pool_unlock(pool);
    return ok;
}

static int channel_write(void *context, const char *bytes, size_t n)
{
    ParallelWorker *worker;
    size_t at;
    size_t take;

    worker = (ParallelWorker *)context;
    at = 0;
    while (at < n) {
        if (worker->used == sizeof worker->bytes && !worker_publish(worker)) {
            errno = ECANCELED;
            return 0;
        }
        take = sizeof worker->bytes - worker->used;
        if (take > n - at)
            take = n - at;
        memcpy(worker->bytes + worker->used, bytes + at, take);
        worker->used += take;
        at += take;
    }
    return 1;
}

static JobState worker_scan(ParallelWorker *worker, CuVec *buffer)
{
    struct stat st;
    CuInput input;
    FILE *file;
    Search search;
    ScanResult result;
    int fd;
    int closed;
    int saved;

    result.matched = 0;
    result.error = SCAN_OK;
    result.system_error = 0;
    errno = 0;
    fd = open(worker->path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        result.error = SCAN_IO;
        result.system_error = errno;
    } else if (fstat(fd, &st) != 0) {
        saved = errno;
        close(fd);
        result.error = SCAN_IO;
        result.system_error = saved;
    } else if (!S_ISREG(st.st_mode)) {
        close(fd);
        return JOB_SERIAL;
    } else {
        file = fdopen(fd, "rb");
        if (file == NULL) {
            saved = errno;
            close(fd);
            result.error = SCAN_IO;
            result.system_error = saved;
        } else {
            search = *worker->pool->search;
            search.out = NULL;
            search.write = channel_write;
            search.cancelled = worker_cancelled;
            search.context = worker;
            cu_input_attach(&input, file);
            result = search_stream(&search, worker->path, &input, buffer);
            closed = cu_input_close(&input);
            if (!closed && result.error == SCAN_OK) {
                result.error = SCAN_IO;
                result.system_error = cu_input_errno(&input);
            }
            errno = 0;
            closed = fclose(file);
            saved = errno;
            if (closed != 0 && result.error == SCAN_OK) {
                result.error = SCAN_IO;
                result.system_error = saved;
            }
        }
    }
    if (worker->used != 0)
        worker_publish(worker);
    worker->result = result;
    return JOB_DONE;
}

static void *worker_main(void *context)
{
    ParallelWorker *worker;
    ParallelPool *pool;
    CuArena arena;
    CuVec buffer;
    JobState terminal;

    worker = (ParallelWorker *)context;
    pool = worker->pool;
    cu_arena_init(&arena);
    CU_VEC_INIT(&buffer, &arena, char);
    pool_lock(pool);
    for (;;) {
        while (worker->state != JOB_ASSIGNED && !pool->cancel)
            worker_wait(worker);
        if (pool->cancel)
            break;
        worker->state = JOB_RUNNING;
        pool_unlock(pool);
        terminal = worker_scan(worker, &buffer);
        pool_lock(pool);
        worker->state = terminal;
        worker_signal(worker);
    }
    pool_unlock(pool);
    cu_arena_release(&arena);
    return NULL;
}

static int regular_paths(char **paths, int count)
{
    struct stat st;
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(paths[i], "-") == 0 || stat(paths[i], &st) != 0 || !S_ISREG(st.st_mode))
            return 0;
    }
    return 1;
}

static void pool_cancel(ParallelPool *pool, int initialized)
{
    int i;

    pool_lock(pool);
    pool->cancel = 1;
    for (i = 0; i < initialized; i++)
        worker_signal(&pool->workers[i]);
    pool_unlock(pool);
}

/* Not selected => caller retains the serial path. Selected => joined scope. */
static int parallel_search(const Search *search, char **paths, int count, int jobs,
                           FILE *in, FILE *out, FILE *err, int *status)
{
    ParallelPool *pool;
    ParallelWorker *worker;
    pthread_attr_t attributes;
    ScanResult result;
    CuArena serial_arena;
    CuVec serial_buffer;
    int initialized;
    int created;
    int i;
    int next;
    int code;
    int matched;
    int failed;
    int attr_ready;
    JobState state;

    if (!regular_paths(paths, count))
        return 0;
    if (jobs > count)
        jobs = count;
    pool = (ParallelPool *)CGREP_POOL_ALLOC(sizeof *pool);
    if (pool == NULL) {
        fputs("cgrep: out of memory starting workers\n", err);
        *status = 2;
        return 1;
    }
    pool->cancel = 0;
    pool->search = search;
    code = pthread_mutex_init(&pool->mutex, NULL);
    if (code != 0) {
        free(pool);
        fprintf(err, "cgrep: worker startup: %s\n", strerror(code));
        *status = 2;
        return 1;
    }
    initialized = 0;
    created = 0;
    attr_ready = 0;
    for (i = 0; i < jobs; i++) {
        worker = &pool->workers[i];
        worker->pool = pool;
        worker->path = NULL;
        worker->state = JOB_IDLE;
        worker->used = 0;
        worker->result.matched = 0;
        worker->result.error = SCAN_OK;
        worker->result.system_error = 0;
        code = pthread_cond_init(&worker->changed, NULL);
        if (code != 0)
            break;
        initialized++;
    }
    if (code == 0) {
        code = pthread_attr_init(&attributes);
        attr_ready = code == 0;
    }
    if (code == 0)
        code = pthread_attr_setstacksize(&attributes, (size_t)262144);
    if (code == 0) {
        for (i = 0; i < jobs; i++) {
            code = CGREP_THREAD_CREATE(&pool->workers[i].thread, &attributes, worker_main, &pool->workers[i]);
            if (code != 0)
                break;
            created++;
        }
    }
    if (attr_ready)
        pthread_attr_destroy(&attributes);
    matched = 0;
    failed = code != 0;
    cu_arena_init(&serial_arena);
    CU_VEC_INIT(&serial_buffer, &serial_arena, char);
    if (code != 0) {
        fprintf(err, "cgrep: worker startup: %s\n", strerror(code));
    } else {
        pool_lock(pool);
        for (i = 0; i < jobs; i++) {
            pool->workers[i].path = paths[i];
            pool->workers[i].state = JOB_ASSIGNED;
            worker_signal(&pool->workers[i]);
        }
        pool_unlock(pool);
        for (i = 0; i < count; i++) {
            worker = &pool->workers[i % jobs];
            pool_lock(pool);
            for (;;) {
                while (worker->state != JOB_CHUNK && worker->state != JOB_DONE && worker->state != JOB_SERIAL)
                    worker_wait(worker);
                if (worker->state != JOB_CHUNK)
                    break;
                if (fwrite(worker->bytes, 1, worker->used, out) != worker->used || fflush(out) != 0 || ferror(out)) {
                    pool_unlock(pool);
                    failed = 1;
                    fputs("cgrep: output error\n", err);
                    goto joined_cleanup;
                }
                worker->used = 0;
                worker->state = JOB_RUNNING;
                worker_signal(worker);
            }
            state = worker->state;
            result = worker->result;
            pool_unlock(pool);
            if (state == JOB_SERIAL) {
                code = search_input(search, paths[i], in, err, &serial_buffer);
                if (code < 0)
                    failed = 1;
                else
                    matched |= code;
            } else {
                matched |= result.matched;
                if (result.error != SCAN_OK) {
                    fprintf(err, "cgrep: %s: %s\n", paths[i], scan_error(result));
                    failed = 1;
                }
            }
            if (fflush(out) != 0 || ferror(out)) {
                failed = 1;
                fputs("cgrep: output error\n", err);
                goto joined_cleanup;
            }
            next = i + jobs;
            pool_lock(pool);
            worker->used = 0;
            if (next < count) {
                worker->path = paths[next];
                worker->state = JOB_ASSIGNED;
            } else {
                worker->state = JOB_IDLE;
            }
            worker_signal(worker);
            pool_unlock(pool);
        }
    }
joined_cleanup:
    pool_cancel(pool, initialized);
    for (i = 0; i < created; i++) {
        if (pthread_join(pool->workers[i].thread, NULL) != 0)
            abort();
    }
    for (i = 0; i < initialized; i++)
        pthread_cond_destroy(&pool->workers[i].changed);
    pthread_mutex_destroy(&pool->mutex);
    cu_arena_release(&serial_arena);
    free(pool);
    if (fflush(out) != 0 || ferror(out))
        failed = 1;
    *status = failed ? 2 : matched ? 0 : 1;
    return 1;
}
#else
static int parallel_search(const Search *search, char **paths, int count, int jobs,
                           FILE *in, FILE *out, FILE *err, int *status)
{
    (void)search;
    (void)paths;
    (void)count;
    (void)jobs;
    (void)in;
    (void)out;
    (void)err;
    (void)status;
    return 0;
}
#endif
