/* Native ordered worker scope. Included after the platform-independent scanner. */
#if defined(__linux__) && defined(__GLIBC__) && defined(_GNU_SOURCE) && !defined(__cerb__)
#if __GLIBC_PREREQ(2, 34)
#define CGREP_NATIVE_PARALLEL 1
#endif
#endif

#ifdef CGREP_NATIVE_PARALLEL
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef CGREP_POOL_ALLOC
#define CGREP_POOL_ALLOC malloc
#endif
#define CHANNEL_SLOTS 2
#ifndef CHANNEL_BYTES
#define CHANNEL_BYTES 1048576
#endif
/* Workers poll the shared cancellation flag once per this many refills. */
#define CANCEL_POLL 8

/* FREE slots belong to their worker; published slots to the coordinator. */
typedef enum { SLOT_FREE, SLOT_CHUNK, SLOT_DONE, SLOT_SERIAL } SlotState;
typedef struct ParallelPool ParallelPool;
typedef struct {
    SlotState state;
    size_t used;
    ScanResult result;
    char bytes[CHANNEL_BYTES];
} ParallelSlot;

/* Worker w scans paths w, w + jobs, ... and publishes them to alternating
 * slots; the coordinator drains them in the same alternation, so argument
 * order holds while a worker scans one file ahead of consumption. */
typedef struct {
    ParallelPool *pool;
    CuThread *thread;
    CuCond *changed;
    int first;
    int fill;  /* worker-written slot */
    int drain; /* next slot the coordinator consumes */
    unsigned polls;
    const char *path;
    int walked;  /* path was found under a directory argument */
    char *bytes; /* the fill slot's storage while scanning */
    size_t used;
    ScanResult result;
    ParallelSlot slots[CHANNEL_SLOTS];
} ParallelWorker;

struct ParallelPool {
    CuMutex *mutex;
    int cancel;
    int jobs;
    int count;
    char **paths;
    const unsigned char *walked; /* per path, or NULL for none */
    const Search *search;
    ParallelWorker *workers; /* one per started job */
};

static void pool_lock(ParallelPool *pool)
{
    cu_mutex_lock(pool->mutex);
}

static void pool_unlock(ParallelPool *pool)
{
    cu_mutex_unlock(pool->mutex);
}

static void worker_wait(ParallelWorker *worker)
{
    cu_cond_wait(worker->changed, worker->pool->mutex);
}

/* The worker and the coordinator can both wait on one condition. */
static void worker_signal(ParallelWorker *worker)
{
    cu_cond_broadcast(worker->changed);
}

static int worker_cancelled(void *context)
{
    ParallelWorker *worker;
    int cancel;

    worker = (ParallelWorker *)context;
    if (++worker->polls % CANCEL_POLL != 0)
        return 0;
    pool_lock(worker->pool);
    cancel = worker->pool->cancel;
    pool_unlock(worker->pool);
    return cancel;
}

/* Hand the filled slot to the coordinator and continue in the other one
 * once it is free. Zero means cancellation. */
static int worker_publish(ParallelWorker *worker)
{
    ParallelPool *pool;
    ParallelSlot *slot;
    int ok;

    pool = worker->pool;
    pool_lock(pool);
    if (!pool->cancel) {
        slot = &worker->slots[worker->fill];
        slot->used = worker->used;
        slot->state = SLOT_CHUNK;
        worker->fill = (worker->fill + 1) % CHANNEL_SLOTS;
        worker_signal(worker);
        while (worker->slots[worker->fill].state != SLOT_FREE && !pool->cancel)
            worker_wait(worker);
        worker->bytes = worker->slots[worker->fill].bytes;
        worker->used = 0;
    }
    ok = !pool->cancel;
    pool_unlock(pool);
    return ok;
}

static int channel_write(void *context, const CuStr *parts, size_t count)
{
    ParallelWorker *worker;
    size_t i;
    size_t at;
    size_t take;
    size_t room;
    size_t total;

    worker = (ParallelWorker *)context;
    room = CHANNEL_BYTES - worker->used;
    total = 0;
    for (i = 0; i < count; i++) {
        if (parts[i].len > room - total)
            break;
        total += parts[i].len;
    }
    /* Fitting records stay private; exact fills still publish lazily. */
    if (i == count) {
        at = worker->used;
        for (i = 0; i < count; i++) {
            if (parts[i].len == 1) {
                worker->bytes[at++] = parts[i].ptr[0];
            } else if (parts[i].len != 0) {
                memcpy(worker->bytes + at, parts[i].ptr, parts[i].len);
                at += parts[i].len;
            }
        }
        worker->used = at;
        return 1;
    }
    for (i = 0; i < count; i++) {
        at = 0;
        while (at < parts[i].len) {
            if (worker->used == CHANNEL_BYTES && !worker_publish(worker)) {
                errno = ECANCELED;
                return 0;
            }
            take = CHANNEL_BYTES - worker->used;
            if (take > parts[i].len - at)
                take = parts[i].len - at;
            memcpy(worker->bytes + worker->used, parts[i].ptr + at, take);
            worker->used += take;
            at += take;
        }
    }
    return 1;
}

static SlotState worker_scan(ParallelWorker *worker, CuVec *buffer, CuVec *scratch)
{
    struct stat st;
    CuInput input;
    FILE *file;
    Search search;
    Finder finder;
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
        return SLOT_SERIAL;
    } else {
        file = fdopen(fd, "rb");
        if (file == NULL) {
            saved = errno;
            close(fd);
            result.error = SCAN_IO;
            result.system_error = saved;
        } else {
            search = *worker->pool->search;
            search.finder = &finder;
            search.scratch = scratch;
            search.walked = worker->walked;
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
    /* Terminal publication transfers any residual bytes with the result.
     * Tiny files need no separate chunk/ack round trip. */
    worker->result = result;
    return SLOT_DONE;
}

static void worker_main(void *context)
{
    ParallelWorker *worker;
    ParallelPool *pool;
    ParallelSlot *slot;
    CuArena arena;
    CuArena scratch_arena;
    CuVec buffer;
    CuVec scratch;
    SlotState terminal;
    int k;

    worker = (ParallelWorker *)context;
    pool = worker->pool;
    cu_arena_init(&arena);
    CU_VEC_INIT(&buffer, &arena, char);
    cu_arena_init(&scratch_arena);
    CU_VEC_INIT(&scratch, &scratch_arena, char);
    pool_lock(pool);
    for (k = worker->first; k < pool->count; k += pool->jobs) {
        while (worker->slots[worker->fill].state != SLOT_FREE && !pool->cancel)
            worker_wait(worker);
        if (pool->cancel)
            break;
        worker->path = pool->paths[k];
        worker->walked = pool->walked != NULL && pool->walked[k];
        worker->bytes = worker->slots[worker->fill].bytes;
        worker->used = 0;
        pool_unlock(pool);
        terminal = worker_scan(worker, &buffer, &scratch);
        pool_lock(pool);
        /* After cancellation the fill slot may not be free; publish nothing. */
        if (pool->cancel)
            break;
        slot = &worker->slots[worker->fill];
        slot->used = worker->used;
        slot->result = worker->result;
        slot->state = terminal;
        worker->fill = (worker->fill + 1) % CHANNEL_SLOTS;
        worker_signal(worker);
    }
    pool_unlock(pool);
    cu_arena_release(&scratch_arena);
    cu_arena_release(&arena);
}

/* Workers never read standard input. Other non-regular paths fall back to
 * serial search when the coordinator reaches them. */
static int named_paths(char **paths, int count)
{
    int i;

    for (i = 0; i < count; i++) {
        if (strcmp(paths[i], "-") == 0)
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
static int parallel_search(const Search *search, char **paths, const unsigned char *walked, int count,
                           int jobs, FILE *in, FILE *out, FILE *err, int *status)
{
    ParallelPool *pool;
    ParallelWorker *worker;
    ParallelSlot *slot;
    ScanResult result;
    CuArena thread_arena;
    CuArena serial_arena;
    CuVec serial_buffer;
    int initialized;
    int created;
    int i;
    int s;
    int code;
    int matched;
    int failed;
    int unavailable;
    size_t used;
    SlotState state;

    if (!named_paths(paths, count))
        return 0;
    if (jobs > count)
        jobs = count;
    pool = (ParallelPool *)CGREP_POOL_ALLOC(sizeof *pool);
    if (pool == NULL) {
        fputs("cgrep: out of memory starting workers\n", err);
        *status = 2;
        return 1;
    }
    pool->workers = (ParallelWorker *)CGREP_POOL_ALLOC((size_t)jobs * sizeof *pool->workers);
    if (pool->workers == NULL) {
        free(pool);
        fputs("cgrep: out of memory starting workers\n", err);
        *status = 2;
        return 1;
    }
    pool->cancel = 0;
    pool->jobs = jobs;
    pool->count = count;
    pool->paths = paths;
    pool->walked = walked;
    pool->search = search;
    cu_arena_init(&thread_arena);
    if (!cu_mutex_init(&pool->mutex, &thread_arena)) {
        code = errno;
        cu_arena_release(&thread_arena);
        free(pool->workers);
        free(pool);
        if (code == ENOSYS)
            return 0;
        fprintf(err, "cgrep: worker startup: %s\n", strerror(code));
        *status = 2;
        return 1;
    }
    initialized = 0;
    created = 0;
    code = 0;
    for (i = 0; i < jobs; i++) {
        worker = &pool->workers[i];
        worker->pool = pool;
        worker->first = i;
        worker->fill = 0;
        worker->drain = 0;
        worker->polls = 0;
        worker->path = NULL;
        worker->walked = 0;
        worker->bytes = NULL;
        worker->used = 0;
        worker->result.matched = 0;
        worker->result.error = SCAN_OK;
        worker->result.system_error = 0;
        for (s = 0; s < CHANNEL_SLOTS; s++) {
            worker->slots[s].state = SLOT_FREE;
            worker->slots[s].used = 0;
        }
        if (!cu_cond_init(&worker->changed, &thread_arena)) {
            code = errno;
            break;
        }
        initialized++;
    }
    if (code == 0) {
        for (i = 0; i < jobs; i++) {
            if (!cu_thread_start(&pool->workers[i].thread, &thread_arena,
                                 (size_t)262144, worker_main, &pool->workers[i])) {
                code = errno;
                break;
            }
            created++;
        }
    }
    matched = 0;
    failed = code != 0;
    unavailable = code == ENOSYS;
    cu_arena_init(&serial_arena);
    CU_VEC_INIT(&serial_buffer, &serial_arena, char);
    if (code != 0) {
        if (!unavailable)
            fprintf(err, "cgrep: worker startup: %s\n", strerror(code));
    } else {
        for (i = 0; i < count; i++) {
            worker = &pool->workers[i % jobs];
            pool_lock(pool);
            for (;;) {
                slot = &worker->slots[worker->drain];
                while (slot->state == SLOT_FREE)
                    worker_wait(worker);
                state = slot->state;
                used = slot->used;
                result = slot->result;
                if (used != 0) {
                    /* A published slot's bytes stay frozen until it is freed.
                     * No output I/O while holding the shared pool mutex. */
                    pool_unlock(pool);
                    if (fwrite(slot->bytes, 1, used, out) != used || fflush(out) != 0 || ferror(out)) {
                        failed = 1;
                        fputs("cgrep: output error\n", err);
                        goto joined_cleanup;
                    }
                    pool_lock(pool);
                }
                slot->used = 0;
                slot->state = SLOT_FREE;
                worker->drain = (worker->drain + 1) % CHANNEL_SLOTS;
                worker_signal(worker);
                if (state != SLOT_CHUNK)
                    break;
            }
            pool_unlock(pool);
            if (state == SLOT_SERIAL) {
                code = search_input(search, paths[i], walked != NULL && walked[i], in, err, &serial_buffer);
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
        }
    }
joined_cleanup:
    pool_cancel(pool, initialized);
    for (i = 0; i < created; i++)
        cu_thread_join(pool->workers[i].thread);
    for (i = 0; i < initialized; i++)
        cu_cond_destroy(pool->workers[i].changed);
    cu_mutex_destroy(pool->mutex);
    cu_arena_release(&thread_arena);
    cu_arena_release(&serial_arena);
    free(pool->workers);
    free(pool);
    if (unavailable)
        return 0;
    if (fflush(out) != 0 || ferror(out))
        failed = 1;
    *status = failed ? 2 : matched ? 0 : 1;
    return 1;
}
#else
static int parallel_search(const Search *search, char **paths, const unsigned char *walked, int count,
                           int jobs, FILE *in, FILE *out, FILE *err, int *status)
{
    (void)search;
    (void)paths;
    (void)walked;
    (void)count;
    (void)jobs;
    (void)in;
    (void)out;
    (void)err;
    (void)status;
    return 0;
}
#endif
