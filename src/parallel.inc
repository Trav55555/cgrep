/* Native ordered worker scope. Included after the lister and the serial scanner. */
#if defined(__linux__) && defined(__GLIBC__) && defined(_GNU_SOURCE) && !defined(__cerb__)
#if __GLIBC_PREREQ(2, 34)
#define CGREP_NATIVE_PARALLEL 1
#endif
#endif

#ifdef CGREP_NATIVE_PARALLEL
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#ifndef CGREP_POOL_ALLOC
#define CGREP_POOL_ALLOC malloc
#endif
#ifndef CGREP_POOL_FREE
#define CGREP_POOL_FREE free
#endif
#define CHANNEL_SLOTS 2
#ifndef CHANNEL_BYTES
#define CHANNEL_BYTES 1048576
#endif
/* Workers poll the shared cancellation flag once per this many refills. */
#define CANCEL_POLL 8
/* A claim takes up to BATCH_MAX consecutive inputs. Each worker's claims
 * start at CGREP_BATCH_START inputs and double while the inputs it read
 * stay small, aiming at BATCH_BYTES of input per claim. */
#ifndef CGREP_BATCH_START
#define CGREP_BATCH_START 1
#endif
#define BATCH_MAX 64
#define BATCH_BYTES 4194304UL
/* A worker starts once this many unclaimed inputs are waiting per worker. */
#ifndef CGREP_START_BACKLOG
#define CGREP_START_BACKLOG 16
#endif
#define MAX_WORKERS 32
#define WORKER_STACK ((size_t)262144)
#define WALKER_STACK ((size_t)1048576)
/* Each worker's stdio buffer: set before the first read, so glibc needs
 * no fstat to size one, and large enough that a small file is one read. */
#define INPUT_BUFFER 65536

/* FREE slots belong to their worker; published slots to the coordinator.
 * CHUNK: more of the batch follows in the worker's other slot. DONE: the
 * batch ends in this slot. */
typedef enum { SLOT_FREE, SLOT_CHUNK, SLOT_DONE } SlotState;

/* One input of a batch, recorded in the slot where its output ends. */
typedef struct {
    const char *path;
    size_t end; /* offset in the slot after the input's bytes */
    ScanResult result;
    unsigned char walked;
    unsigned char serial; /* not a regular file: the coordinator searches it */
} ParallelRecord;

typedef struct {
    SlotState state;
    size_t used;
    int records;
    ParallelRecord record[BATCH_MAX];
    char bytes[CHANNEL_BYTES];
} ParallelSlot;

/* A claimed batch: inputs start .. start + count - 1, owned by worker. */
typedef struct {
    int worker;
    int start;
    int count;
} ParallelBatch;

/* A worker claims a batch of consecutive inputs once its fill slot is
 * free, scans them into that slot (spilling full slots as CHUNKs) and
 * publishes the batch as DONE. The coordinator drains batches in claim
 * order, which is input order, so the output order holds. */
typedef struct {
    ParallelPool *pool;
    CuThread *thread;
    CuCond *changed;
    int index;
    int fill;  /* worker-written slot */
    int drain; /* next slot the coordinator consumes */
    unsigned polls;
    int batch;     /* inputs to claim next */
    int idle;      /* waiting on changed for inputs; written under the mutex */
    int cancelled; /* observed cancellation: the batch is abandoned */
    const char *path;
    int walked;
    char *bytes; /* the fill slot's storage while scanning */
    size_t used;
    char *paths[BATCH_MAX];
    unsigned char walked_flags[BATCH_MAX];
    char input[INPUT_BUFFER];
    ParallelSlot slots[CHANNEL_SLOTS];
} ParallelWorker;

struct ParallelPool {
    CuMutex *mutex;
    /* The coordinator's: inputs published, a batch claimed, listing
     * finished. Idle workers wait on their own condition instead. */
    CuCond *changed;
    int cancel;
    int jobs;    /* workers allowed */
    int started; /* workers running; written by the coordinator */
    Listing *listing;
    int count;  /* published inputs */
    int next;   /* first unclaimed input */
    int listed; /* list_inputs has returned */
    int listing_failed;
    unsigned long claims;
    /* Claimed batches not yet consumed. A worker holds at most two (one
     * published, one being scanned), so the ring never fills. */
    ParallelBatch ring[2 * MAX_WORKERS + 2];
    int ring_head;
    int ring_len;
    /* The walker thread's diagnostics: a memory stream it writes outside
     * the mutex and copies into diagnostics under it; the coordinator
     * takes and prints them. */
    FILE *walker_err;
    char *walker_text;
    size_t walker_size;
    size_t walker_seen;
    CuArena diagnostic_arena;
    CuVec diagnostics;
    const Search *search;
    ParallelWorker *workers[MAX_WORKERS]; /* allocated as they start */
};

static void pool_lock(ParallelPool *pool)
{
    cu_mutex_lock(pool->mutex);
}

static void pool_unlock(ParallelPool *pool)
{
    cu_mutex_unlock(pool->mutex);
}

static void pool_wait(ParallelPool *pool)
{
    cu_cond_wait(pool->changed, pool->mutex);
}

static void pool_signal(ParallelPool *pool)
{
    cu_cond_broadcast(pool->changed);
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

/* Wakes idle workers until their next claims cover available inputs, or
 * every idle worker when all is set (the listing ended). Waking each one
 * for every publication cost more than the search on small files. Holds
 * the mutex. */
static void wake_idle(ParallelPool *pool, int available, int all)
{
    ParallelWorker *worker;
    int i;

    for (i = 0; i < pool->started && (all || available > 0); i++) {
        worker = pool->workers[i];
        if (worker->idle) {
            worker->idle = 0;
            available -= worker->batch;
            worker_signal(worker);
        }
    }
}

/* Copies what the walker wrote since the last call. Holds the mutex. */
static void publish_diagnostics(ParallelPool *pool)
{
    size_t n;

    if (pool->walker_err == NULL || fflush(pool->walker_err) != 0 || pool->walker_size <= pool->walker_seen)
        return;
    n = pool->walker_size - pool->walker_seen;
    if (cu_vec_reserve(&pool->diagnostics, n)) {
        memcpy(pool->diagnostics.data + pool->diagnostics.len, pool->walker_text + pool->walker_seen, n);
        pool->diagnostics.len += n;
        pool->walker_seen = pool->walker_size;
    }
}

/* Moves published diagnostics to the coordinator's buffer, or prints them
 * when that cannot grow. Holds the mutex. */
static void take_diagnostics(ParallelPool *pool, CuVec *into, FILE *err)
{
    size_t n;

    n = cu_vec_len(&pool->diagnostics);
    if (n == 0)
        return;
    if (cu_vec_reserve(into, n)) {
        memcpy(into->data + into->len, pool->diagnostics.data, n);
        into->len += n;
    } else {
        fwrite(pool->diagnostics.data, 1, n, err);
    }
    cu_vec_clear(&pool->diagnostics);
}

static int inputs_publish(Inputs *inputs)
{
    ParallelPool *pool;
    size_t len;
    int f;
    int ok;

    pool = inputs->pool;
    pool_lock(pool);
    for (f = 0; f < inputs->pending; f++) {
        CU_VEC_PUSH(&inputs->paths, &inputs->pending_paths[f]);
        CU_VEC_PUSH(&inputs->walked, &inputs->pending_walked[f]);
    }
    inputs->pending = 0;
    len = cu_vec_len(&inputs->paths);
    ok = !cu_vec_failed(&inputs->paths) && !cu_vec_failed(&inputs->walked) && len <= INT_MAX;
    if (ok)
        pool->count = (int)len;
    if (pool->cancel)
        inputs->stop = 1;
    publish_diagnostics(pool);
    pool_signal(pool);
    wake_idle(pool, pool->count - pool->next, 0);
    pool_unlock(pool);
    return ok;
}

static int inputs_cancelled(Inputs *inputs)
{
    pool_lock(inputs->pool);
    inputs->stop |= inputs->pool->cancel;
    pool_unlock(inputs->pool);
    return inputs->stop;
}

/* Prefetch threads list the directories the walk will enter, so its
 * open/read/close calls overlap: the walker keeps every decision (order,
 * ignore rules, reports) and only takes finished listings. The most
 * recently queued directory is listed first, which in a depth-first walk
 * is usually the next one entered. */
#define PREFETCH_THREADS 4
/* Threads wanted, at most PREFETCH_THREADS and the job count; 0 lists in
 * the walker alone. */
#ifndef CGREP_PREFETCH
#define CGREP_PREFETCH PREFETCH_THREADS
#endif
/* Listings started or finished but not yet taken, at most. */
#define PREFETCH_AHEAD 256
#define PREFETCH_STACK ((size_t)262144)

struct Prefetch {
    CuMutex *mutex;
    CuCond *work; /* queued, room freed, or stopping */
    CuCond *done; /* a listing finished */
    int stop;
    int skip_hidden;
    int ahead;
    int waiting; /* the walker waits on done */
    CuArena arena; /* stack and all; touched under mutex */
    CuVec stack;   /* DirFetch *, the next to list on top */
    CuVec all;     /* every queued DirFetch, to release unused listings */
    CuArena thread_arena;
    int want;    /* threads to start at the first queue */
    int threads; /* started */
    CuThread *thread[PREFETCH_THREADS];
};

static void prefetch_main(void *context)
{
    Prefetch *pf;
    DirFetch *f;

    pf = (Prefetch *)context;
    cu_mutex_lock(pf->mutex);
    for (;;) {
        while (!pf->stop && (cu_vec_len(&pf->stack) == 0 || pf->ahead >= PREFETCH_AHEAD))
            cu_cond_wait(pf->work, pf->mutex);
        if (pf->stop)
            break;
        (void)CU_VEC_POP(&pf->stack, &f);
        /* The walker claims a queued directory it reaches first. */
        if (f->state != FETCH_QUEUED)
            continue;
        f->state = FETCH_RUNNING;
        pf->ahead++;
        cu_mutex_unlock(pf->mutex);
        cu_arena_init(&f->arena);
        f->arena.block_size = 4096;
        read_dir(&f->arena, f->path, pf->skip_hidden, &f->list);
        cu_mutex_lock(pf->mutex);
        f->state = FETCH_DONE;
        if (pf->waiting)
            cu_cond_broadcast(pf->done);
    }
    cu_mutex_unlock(pf->mutex);
}

static void prefetch_queue(Prefetch *pf, DirFetch **fetches, size_t count)
{
    size_t i;

    /* A walk that never enters a subdirectory starts no threads. Only the
     * walker starts them, before any can touch pf. */
    if (pf->want != 0) {
        while (pf->threads < pf->want &&
               cu_thread_start(&pf->thread[pf->threads], &pf->thread_arena, PREFETCH_STACK, prefetch_main, pf))
            pf->threads++;
        pf->want = 0;
    }
    if (pf->threads == 0)
        return;
    cu_mutex_lock(pf->mutex);
    /* Pushed last-first, so the first to enter is on top. */
    for (i = count; i-- > 0;) {
        CU_VEC_PUSH(&pf->all, &fetches[i]);
        if (cu_vec_failed(&pf->all))
            break;
        CU_VEC_PUSH(&pf->stack, &fetches[i]);
        if (cu_vec_failed(&pf->stack))
            break;
        fetches[i]->state = FETCH_QUEUED;
    }
    cu_cond_broadcast(pf->work);
    cu_mutex_unlock(pf->mutex);
}

static int prefetch_take(Prefetch *pf, DirFetch *f)
{
    int ready;

    cu_mutex_lock(pf->mutex);
    if (f->state == FETCH_QUEUED)
        f->state = FETCH_TAKEN;
    while (f->state == FETCH_RUNNING) {
        pf->waiting = 1;
        cu_cond_wait(pf->done, pf->mutex);
    }
    pf->waiting = 0;
    ready = f->state == FETCH_DONE;
    if (ready) {
        f->state = FETCH_TAKEN;
        if (pf->ahead-- == PREFETCH_AHEAD)
            cu_cond_broadcast(pf->work);
    }
    cu_mutex_unlock(pf->mutex);
    return ready;
}

/* Prepares up to want threads, started by the first prefetch_queue. Zero
 * means nothing is held. */
static int prefetch_start(Prefetch *pf, const Listing *l, int want)
{
    pf->stop = 0;
    pf->skip_hidden = !l->options.hidden && !l->ignoring;
    pf->ahead = 0;
    pf->waiting = 0;
    pf->threads = 0;
    cu_arena_init(&pf->arena);
    cu_arena_init(&pf->thread_arena);
    CU_VEC_INIT(&pf->stack, &pf->arena, DirFetch *);
    CU_VEC_INIT(&pf->all, &pf->arena, DirFetch *);
    if (want > CGREP_PREFETCH)
        want = CGREP_PREFETCH;
    if (want < 1 || !cu_mutex_init(&pf->mutex, &pf->thread_arena)) {
        cu_arena_release(&pf->thread_arena);
        return 0;
    }
    if (!cu_cond_init(&pf->work, &pf->thread_arena)) {
        cu_mutex_destroy(pf->mutex);
        cu_arena_release(&pf->thread_arena);
        return 0;
    }
    if (!cu_cond_init(&pf->done, &pf->thread_arena)) {
        cu_cond_destroy(pf->work);
        cu_mutex_destroy(pf->mutex);
        cu_arena_release(&pf->thread_arena);
        return 0;
    }
    pf->want = want;
    return 1;
}

/* Joins the threads and releases listings the walk never took. */
static void prefetch_stop(Prefetch *pf)
{
    DirFetch **all;
    size_t i;
    int t;

    cu_mutex_lock(pf->mutex);
    pf->stop = 1;
    cu_cond_broadcast(pf->work);
    cu_mutex_unlock(pf->mutex);
    for (t = 0; t < pf->threads; t++)
        cu_thread_join(pf->thread[t]);
    all = CU_VEC_DATA(&pf->all, DirFetch *);
    for (i = 0; i < cu_vec_len(&pf->all); i++)
        if (all[i]->state == FETCH_DONE)
            cu_arena_release(&all[i]->arena);
    cu_cond_destroy(pf->done);
    cu_cond_destroy(pf->work);
    cu_mutex_destroy(pf->mutex);
    cu_arena_release(&pf->thread_arena);
    cu_arena_release(&pf->arena);
}

static void walker_main(void *context)
{
    ParallelPool *pool;
    Prefetch prefetch;
    int prefetching;
    int ok;

    pool = (ParallelPool *)context;
    prefetching = prefetch_start(&prefetch, pool->listing, pool->jobs);
    if (prefetching)
        pool->listing->inputs.prefetch = &prefetch;
    ok = list_inputs(pool->listing, pool->walker_err);
    if (prefetching) {
        pool->listing->inputs.prefetch = NULL;
        prefetch_stop(&prefetch);
    }
    pool_lock(pool);
    publish_diagnostics(pool);
    pool->listing_failed = !ok;
    pool->listed = 1;
    pool_signal(pool);
    wake_idle(pool, 0, 1);
    pool_unlock(pool);
}

static int worker_cancelled(void *context)
{
    ParallelWorker *worker;

    worker = (ParallelWorker *)context;
    if (++worker->polls % CANCEL_POLL != 0)
        return worker->cancelled;
    pool_lock(worker->pool);
    worker->cancelled |= worker->pool->cancel;
    pool_unlock(worker->pool);
    return worker->cancelled;
}

/* Hand the filled slot to the coordinator and continue in the other one
 * once it is free. Zero means cancellation. */
static int worker_publish(ParallelWorker *worker)
{
    ParallelPool *pool;
    ParallelSlot *slot;

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
    worker->cancelled |= pool->cancel;
    pool_unlock(pool);
    return !worker->cancelled;
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

/* Scans the worker's current input into its slot. Returns 1 when the
 * input is not a regular file, for the coordinator to search in turn. A
 * walked input was a regular file when listed; the nonblocking open keeps
 * a raced FIFO from blocking, so only arguments are classified again. */
static int worker_scan(ParallelWorker *worker, Search *search, CuVec *buffer, ScanResult *out)
{
    CuFileInfo info;
    CuInput input;
    FILE *file;
    ScanResult result;
    int fd;
    int closed;
    int saved;

    result.matched = 0;
    result.error = SCAN_OK;
    result.system_error = 0;
    result.bytes = 0;
    errno = 0;
    fd = open(worker->path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        result.error = SCAN_IO;
        result.system_error = errno;
    } else if (!worker->walked && cu_fd_info(fd, &info) != CU_INFO_OK) {
        saved = errno;
        close(fd);
        result.error = SCAN_IO;
        result.system_error = saved;
    } else if (!worker->walked && info.kind != CU_DIR_FILE) {
        close(fd);
        return 1;
    } else {
        file = fdopen(fd, "rb");
        if (file == NULL) {
            saved = errno;
            close(fd);
            result.error = SCAN_IO;
            result.system_error = saved;
        } else {
            setvbuf(file, worker->input, _IOFBF, INPUT_BUFFER);
            search->walked = worker->walked;
            cu_input_attach(&input, file);
            result = search_stream(search, worker->path, &input, buffer);
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
    *out = result;
    return 0;
}

/* How many of the unclaimed inputs a worker wanting `wanted` takes. Once
 * everything is listed, no claim exceeds a running worker's share, so the
 * tail stays balanced. */
static int claim_size(const ParallelPool *pool, int wanted)
{
    int available;
    int share;

    available = pool->count - pool->next;
    if (pool->listed && pool->started > 1) {
        share = (available + pool->started - 1) / pool->started;
        if (wanted > share)
            wanted = share;
    }
    if (wanted > available)
        wanted = available;
    return wanted < 1 ? 1 : wanted;
}

/* The next claim after one of n inputs holding bytes: twice as many while
 * the average input keeps a claim under BATCH_BYTES, else as many as fit. */
static int next_batch(int batch, unsigned long bytes, int n)
{
    unsigned long want;
    int next;

    want = BATCH_BYTES / (bytes / (unsigned long)n + 1);
    next = batch * 2;
    if ((unsigned long)next > want)
        next = (int)want;
    if (next < 1)
        next = 1;
    return next > BATCH_MAX ? BATCH_MAX : next;
}

/* Records a claim of count inputs from start by worker. Holds the mutex. */
static void pool_claim(ParallelPool *pool, int worker, int start, int count)
{
    ParallelBatch *batch;

    batch = &pool->ring[(pool->ring_head + pool->ring_len) % (int)(sizeof pool->ring / sizeof pool->ring[0])];
    batch->worker = worker;
    batch->start = start;
    batch->count = count;
    pool->ring_len++;
    pool->next = start + count;
    pool->claims++;
    pool_signal(pool);
}

static void worker_main(void *context)
{
    ParallelWorker *worker;
    ParallelPool *pool;
    ParallelSlot *slot;
    ParallelRecord *record;
    CuArena arena;
    CuArena scratch_arena;
    CuVec buffer;
    CuVec scratch;
    Search search;
    Finder finder;
    ScanResult result;
    char **paths;
    const unsigned char *walked;
    unsigned long bytes;
    int start;
    int n;
    int f;
    int serial;

    worker = (ParallelWorker *)context;
    pool = worker->pool;
    cu_arena_init(&arena);
    CU_VEC_INIT(&buffer, &arena, char);
    cu_arena_init(&scratch_arena);
    CU_VEC_INIT(&scratch, &scratch_arena, char);
    search = *pool->search;
    search.finder = &finder;
    search.scratch = &scratch;
    search.out = NULL;
    search.write = channel_write;
    search.cancelled = worker_cancelled;
    search.context = worker;
    pool_lock(pool);
    for (;;) {
        /* A free fill slot first, so a claimed batch can always start. */
        while (worker->slots[worker->fill].state != SLOT_FREE && !pool->cancel)
            worker_wait(worker);
        while (pool->next == pool->count && !pool->listed && !pool->cancel) {
            worker->idle = 1;
            worker_wait(worker);
        }
        worker->idle = 0;
        if (pool->cancel || pool->next == pool->count)
            break;
        n = claim_size(pool, worker->batch);
        start = pool->next;
        pool_claim(pool, worker->index, start, n);
        /* Inputs this claim left are other idle workers' to take. */
        wake_idle(pool, pool->count - pool->next, 0);
        paths = CU_VEC_DATA(&pool->listing->inputs.paths, char *);
        walked = CU_VEC_DATA(&pool->listing->inputs.walked, unsigned char);
        for (f = 0; f < n; f++) {
            worker->paths[f] = paths[start + f];
            worker->walked_flags[f] = walked[start + f];
        }
        worker->bytes = worker->slots[worker->fill].bytes;
        worker->used = 0;
        pool_unlock(pool);
        bytes = 0;
        for (f = 0; f < n; f++) {
            worker->path = worker->paths[f];
            worker->walked = worker->walked_flags[f];
            serial = worker_scan(worker, &search, &buffer, &result);
            if (worker->cancelled)
                break;
            /* The scan may have spilled into the other slot. */
            slot = &worker->slots[worker->fill];
            record = &slot->record[slot->records++];
            record->path = worker->path;
            record->walked = worker->walked_flags[f];
            record->serial = (unsigned char)serial;
            record->end = worker->used;
            if (serial) {
                record->result.matched = 0;
                record->result.error = SCAN_OK;
                record->result.system_error = 0;
                record->result.bytes = 0;
            } else {
                record->result = result;
                bytes += result.bytes;
            }
        }
        pool_lock(pool);
        /* After cancellation the fill slot may not be free; publish nothing. */
        if (pool->cancel || worker->cancelled)
            break;
        slot = &worker->slots[worker->fill];
        slot->used = worker->used;
        slot->state = SLOT_DONE;
        worker->fill = (worker->fill + 1) % CHANNEL_SLOTS;
        worker_signal(worker);
        worker->batch = next_batch(worker->batch, bytes, n);
    }
    pool_unlock(pool);
    cu_arena_release(&scratch_arena);
    cu_arena_release(&arena);
}

static void pool_cancel(ParallelPool *pool, int started)
{
    int i;

    pool_lock(pool);
    pool->cancel = 1;
    pool_signal(pool);
    for (i = 0; i < started; i++)
        worker_signal(pool->workers[i]);
    pool_unlock(pool);
}

/* Allocates and starts the next worker. Zero means it could not start:
 * reported unless the backend is unavailable, and the pool allows no more. */
static int start_worker(ParallelPool *pool, CuArena *thread_arena, FILE *err, int *failed)
{
    ParallelWorker *worker;
    int code;
    int s;

    worker = (ParallelWorker *)CGREP_POOL_ALLOC(sizeof *worker);
    if (worker == NULL) {
        fputs("cgrep: out of memory starting workers\n", err);
        *failed = 1;
        code = ENOMEM;
    } else {
        worker->pool = pool;
        worker->index = pool->started;
        worker->fill = 0;
        worker->drain = 0;
        worker->polls = 0;
        worker->batch = CGREP_BATCH_START;
        worker->idle = 0;
        worker->cancelled = 0;
        worker->path = NULL;
        worker->walked = 0;
        worker->bytes = NULL;
        worker->used = 0;
        for (s = 0; s < CHANNEL_SLOTS; s++) {
            worker->slots[s].state = SLOT_FREE;
            worker->slots[s].used = 0;
            worker->slots[s].records = 0;
        }
        code = 0;
        if (!cu_cond_init(&worker->changed, thread_arena)) {
            code = errno;
        } else if (!cu_thread_start(&worker->thread, thread_arena, WORKER_STACK, worker_main, worker)) {
            code = errno;
            cu_cond_destroy(worker->changed);
        }
        if (code == 0) {
            pool->workers[pool->started] = worker;
            pool_lock(pool);
            pool->started++;
            pool_unlock(pool);
            return 1;
        }
        CGREP_POOL_FREE(worker);
        if (code != ENOSYS) {
            fprintf(err, "cgrep: worker startup: %s\n", strerror(code));
            *failed = 1;
        }
    }
    pool_lock(pool);
    pool->jobs = pool->started;
    pool_unlock(pool);
    return 0;
}

/* Workers wanted for the unclaimed backlog. Holds the mutex. */
static int wanted_workers(const ParallelPool *pool)
{
    int want;

    want = (pool->count - pool->next) / CGREP_START_BACKLOG;
    return want > pool->jobs ? pool->jobs : want;
}

/* Writes a published slot: each record's bytes, then its result exactly as
 * the serial path reports it. Zero means an output failure. */
static int drain_slot(const Search *search, ParallelSlot *slot, FILE *in, FILE *out, FILE *err,
                      CuVec *serial_buffer, int *matched, int *failed)
{
    const ParallelRecord *record;
    size_t at;
    int r;
    int code;

    at = 0;
    for (r = 0; r < slot->records; r++) {
        record = &slot->record[r];
        if (record->end > at && fwrite(slot->bytes + at, 1, record->end - at, out) != record->end - at)
            return 0;
        at = record->end;
        if (record->serial) {
            code = search_input(search, record->path, record->walked, in, err, serial_buffer);
            if (code < 0)
                *failed = 1;
            else
                *matched |= code;
        } else {
            *matched |= record->result.matched;
            if (record->result.error != SCAN_OK) {
                if (fflush(out) != 0)
                    return 0;
                fprintf(err, "cgrep: %s: %s\n", record->path, scan_error(record->result));
                *failed = 1;
            }
        }
    }
    if (slot->used > at && fwrite(slot->bytes + at, 1, slot->used - at, out) != slot->used - at)
        return 0;
    return fflush(out) == 0 && !ferror(out);
}

/* The online processors, at most MAX_WORKERS; one when that is unknown. */
static int default_workers(void)
{
    long n;

    n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 1)
        return 1;
    return n > MAX_WORKERS ? MAX_WORKERS : (int)n;
}

enum { COORDINATE_DONE, COORDINATE_START, COORDINATE_INLINE, COORDINATE_BATCH };

/* Not selected => caller retains the serial path, nothing listed yet.
 * Selected => joined scope: lists the inputs (on a walker thread when a
 * directory argument needs walking), starts workers as the unclaimed
 * backlog grows, searches inputs itself while none has started, and drains
 * claimed batches in order. Only this thread writes out and err. */
static int parallel_search(const Search *search, Listing *listing, int jobs, FILE *in, FILE *out, FILE *err,
                           int *status)
{
    ParallelPool *pool;
    ParallelWorker *worker;
    ParallelSlot *slot;
    ParallelBatch batch;
    CuThread *walker;
    CuArena thread_arena;
    CuArena serial_arena;
    CuVec serial_buffer;
    CuVec pending_err;
    const char *path;
    int walked;
    int i;
    int code;
    int matched;
    int failed;
    int walking;
    int directories;
    int mode;
    int want;
    SlotState state;

    pool = (ParallelPool *)CGREP_POOL_ALLOC(sizeof *pool);
    if (pool == NULL) {
        fputs("cgrep: out of memory starting workers\n", err);
        *status = 2;
        return 1;
    }
    pool->cancel = 0;
    pool->jobs = jobs;
    pool->started = 0;
    pool->listing = listing;
    pool->count = 0;
    pool->next = 0;
    pool->listed = 0;
    pool->listing_failed = 0;
    pool->claims = 0;
    pool->ring_head = 0;
    pool->ring_len = 0;
    pool->walker_err = NULL;
    pool->walker_text = NULL;
    pool->walker_size = 0;
    pool->walker_seen = 0;
    pool->search = search;
    cu_arena_init(&thread_arena);
    if (!cu_mutex_init(&pool->mutex, &thread_arena)) {
        code = errno;
        cu_arena_release(&thread_arena);
        CGREP_POOL_FREE(pool);
        if (code == ENOSYS)
            return 0;
        fprintf(err, "cgrep: worker startup: %s\n", strerror(code));
        *status = 2;
        return 1;
    }
    if (!cu_cond_init(&pool->changed, &thread_arena)) {
        code = errno;
        cu_mutex_destroy(pool->mutex);
        cu_arena_release(&thread_arena);
        CGREP_POOL_FREE(pool);
        if (code == ENOSYS)
            return 0;
        fprintf(err, "cgrep: worker startup: %s\n", strerror(code));
        *status = 2;
        return 1;
    }
    cu_arena_init(&pool->diagnostic_arena);
    CU_VEC_INIT(&pool->diagnostics, &pool->diagnostic_arena, char);
    cu_arena_init(&serial_arena);
    CU_VEC_INIT(&serial_buffer, &serial_arena, char);
    CU_VEC_INIT(&pending_err, &thread_arena, char);
    listing->inputs.pool = pool;
    matched = 0;
    failed = 0;
    walking = 0;
    walker = NULL;
    directories = 0;
    for (i = 0; i < listing->count; i++)
        directories |= listing->directories[i];
    if (directories) {
        pool->walker_err = open_memstream(&pool->walker_text, &pool->walker_size);
        if (pool->walker_err != NULL) {
            if (cu_thread_start(&walker, &thread_arena, WALKER_STACK, walker_main, pool)) {
                walking = 1;
            } else if (errno != ENOSYS) {
                fprintf(err, "cgrep: worker startup: %s\n", strerror(errno));
                failed = 1;
            }
        }
    }
    if (!walking) {
        /* Arguments alone, or no walker thread: list here, before searching. */
        if (pool->walker_err != NULL) {
            fclose(pool->walker_err);
            free(pool->walker_text);
            pool->walker_err = NULL;
        }
        code = list_inputs(listing, err);
        pool_lock(pool);
        pool->listing_failed = !code;
        pool->listed = 1;
        pool_unlock(pool);
    }
    for (;;) {
        pool_lock(pool);
        for (;;) {
            take_diagnostics(pool, &pending_err, err);
            want = wanted_workers(pool);
            if (want > pool->started) {
                mode = COORDINATE_START;
                break;
            }
            if (pool->ring_len != 0) {
                batch = pool->ring[pool->ring_head];
                mode = COORDINATE_BATCH;
                break;
            }
            if (pool->next < pool->count) {
                if (pool->started == 0) {
                    path = CU_VEC_DATA(&listing->inputs.paths, char *)[pool->next];
                    walked = CU_VEC_DATA(&listing->inputs.walked, unsigned char)[pool->next];
                    pool->next++;
                    pool->claims++;
                    mode = COORDINATE_INLINE;
                    break;
                }
                pool_wait(pool);
                continue;
            }
            if (pool->listed) {
                mode = COORDINATE_DONE;
                break;
            }
            pool_wait(pool);
        }
        pool_unlock(pool);
        if (cu_vec_len(&pending_err) != 0) {
            fwrite(pending_err.data, 1, pending_err.len, err);
            cu_vec_clear(&pending_err);
        }
        if (mode == COORDINATE_DONE)
            break;
        if (mode == COORDINATE_START) {
            while (pool->started < want && start_worker(pool, &thread_arena, err, &failed))
                ;
            continue;
        }
        if (mode == COORDINATE_INLINE) {
            code = search_input(search, path, walked, in, err, &serial_buffer);
            if (code < 0)
                failed = 1;
            else
                matched |= code;
            if (fflush(out) != 0 || ferror(out)) {
                failed = 1;
                fputs("cgrep: output error\n", err);
                goto joined_cleanup;
            }
            continue;
        }
        worker = pool->workers[batch.worker];
        for (;;) {
            pool_lock(pool);
            slot = &worker->slots[worker->drain];
            while (slot->state == SLOT_FREE)
                worker_wait(worker);
            state = slot->state;
            pool_unlock(pool);
            /* A published slot's bytes and records stay frozen until it is
             * freed. No output I/O while holding the shared pool mutex. */
            if (!drain_slot(search, slot, in, out, err, &serial_buffer, &matched, &failed)) {
                failed = 1;
                fputs("cgrep: output error\n", err);
                goto joined_cleanup;
            }
            pool_lock(pool);
            slot->used = 0;
            slot->records = 0;
            slot->state = SLOT_FREE;
            worker->drain = (worker->drain + 1) % CHANNEL_SLOTS;
            worker_signal(worker);
            if (state == SLOT_DONE) {
                pool->ring_head = (pool->ring_head + 1) % (int)(sizeof pool->ring / sizeof pool->ring[0]);
                pool->ring_len--;
            }
            pool_unlock(pool);
            if (state == SLOT_DONE)
                break;
        }
    }
joined_cleanup:
    pool_cancel(pool, pool->started);
    if (walking)
        cu_thread_join(walker);
    for (i = 0; i < pool->started; i++)
        cu_thread_join(pool->workers[i]->thread);
    /* Everything is joined: the walker's last diagnostics and failure. */
    publish_diagnostics(pool);
    take_diagnostics(pool, &pending_err, err);
    if (cu_vec_len(&pending_err) != 0)
        fwrite(pending_err.data, 1, pending_err.len, err);
    if (pool->walker_err != NULL) {
        fclose(pool->walker_err);
        free(pool->walker_text);
    }
    failed |= pool->listing_failed;
    for (i = 0; i < pool->started; i++) {
        cu_cond_destroy(pool->workers[i]->changed);
        CGREP_POOL_FREE(pool->workers[i]);
    }
    cu_cond_destroy(pool->changed);
    cu_mutex_destroy(pool->mutex);
    cu_arena_release(&pool->diagnostic_arena);
    cu_arena_release(&thread_arena);
    cu_arena_release(&serial_arena);
    CGREP_POOL_FREE(pool);
    if (fflush(out) != 0 || ferror(out))
        failed = 1;
    *status = failed ? 2 : matched ? 0 : 1;
    return 1;
}
#else
static int inputs_publish(Inputs *inputs)
{
    (void)inputs;
    return 1;
}

/* Without threads nothing is queued: the walk lists every directory. */
static void prefetch_queue(Prefetch *pf, DirFetch **fetches, size_t count)
{
    (void)pf;
    (void)fetches;
    (void)count;
}

static int prefetch_take(Prefetch *pf, DirFetch *f)
{
    (void)pf;
    (void)f;
    return 0;
}

static int inputs_cancelled(Inputs *inputs)
{
    (void)inputs;
    return 0;
}

static int default_workers(void)
{
    return 1;
}

static int parallel_search(const Search *search, Listing *listing, int jobs, FILE *in, FILE *out, FILE *err,
                           int *status)
{
    (void)search;
    (void)listing;
    (void)jobs;
    (void)in;
    (void)out;
    (void)err;
    (void)status;
    return 0;
}
#endif
