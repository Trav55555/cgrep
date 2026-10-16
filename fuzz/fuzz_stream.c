/* Real framing loop with a memory-backed reader and an independent exact
 * output oracle. Successful delivery boundaries are observed, not reproduced
 * using the production buffer algorithm. Inputs reach three 64 KiB refills. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cu.h"
#include "cgrep.h"
#include "stream_oracle.h"

static const uint8_t *input_bytes;
static size_t input_size;
static size_t input_pos;
static size_t step;
static size_t calls;
static size_t fail_at;
static int failed;
static FILE *sink;
static size_t deliveries[FRAME_INPUT_LIMIT];
static size_t delivery_count;

static CuInputStatus memory_read(CuInput *in, void *buf, size_t cap, size_t *n)
{
    assert(cap > 0 && cap <= 65536);
    if (input_pos == 0)
        assert(cap <= 3);
    calls++;
    *n = 0;
    if (calls == fail_at) {
        failed = 1;
        in->state = CU_INPUT_ERROR;
        in->error = EDOM;
        return CU_INPUT_ERROR;
    }
    if (input_pos == input_size)
        return CU_INPUT_END;
    if (cap > step)
        cap = step;
    if (cap > input_size - input_pos)
        cap = input_size - input_pos;
    memcpy(buf, input_bytes + input_pos, cap);
    input_pos += cap;
    assert(delivery_count < sizeof deliveries / sizeof deliveries[0]);
    deliveries[delivery_count++] = input_pos;
    *n = cap;
    return CU_INPUT_DATA;
}

int cgrep_frame_probe(int argc, char **argv, FILE *in, FILE *out, FILE *err);

#define cu_input_read memory_read
#define cgrep_main cgrep_frame_probe
#include "../src/cgrep.c"
#undef cu_input_read
#undef cgrep_main

static void close_sink(void)
{
    if (sink != NULL)
        fclose(sink);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static const size_t large_steps[] = {257, 65535, 65536};
    char pattern[32];
    CuArena a;
    CuVec buffer;
    CuInput in;
    Search searcher;
    ScanResult result;
    size_t n;
    size_t i;
    unsigned mode;
    int expected_match;
    int stopped;

    if (size < 3 || size > FRAME_INPUT_LIMIT)
        return 0;
    if (sink == NULL) {
        sink = tmpfile();
        assert(sink != NULL);
        assert(atexit(close_sink) == 0);
    }
    rewind(sink);
    n = data[0] % (size - 2);
    if (n > sizeof pattern)
        n = sizeof pattern;
    for (i = 0; i < n; i++)
        pattern[i] = data[3 + i] == 0 || data[3 + i] == '\n' ? 'x' : (char)data[3 + i];
    input_bytes = data + 3 + n;
    input_size = size - 3 - n;
    input_pos = 0;
    mode = data[1] >> 6;
    step = mode == 0 ? (size_t)(data[1] >> 2) + 1 : large_steps[mode - 1];
    calls = 0;
    fail_at = data[2];
    failed = 0;
    delivery_count = 0;
    cu_arena_init(&a);
    CU_VEC_INIT(&buffer, &a, char);
    assert(literal_init(&searcher.literal, &a, cu_str_n(pattern, n)));
    search_init(&searcher, sink);
    searcher.numbers = data[1] & 1;
    searcher.names = data[1] & 2;
    searcher.out = sink;
    searcher.write = NULL;
    searcher.cancelled = NULL;
    searcher.finder = NULL;
    searcher.context = NULL;
    cu_input_attach(&in, stdin);
    result = search_stream(&searcher, "input", &in, &buffer);
    expected_match = reference_frame(input_bytes, input_size, pattern, n,
        deliveries, delivery_count, failed, searcher.names, searcher.numbers, &stopped);
    assert(result.matched == expected_match);
    if (failed)
        assert(result.error == SCAN_IO && result.system_error == EDOM);
    else
        assert(result.error == SCAN_OK);
    assert(calls == delivery_count + (stopped ? 0 : 1));
    reference_check_output(sink);
    if (failed)
        assert(!cu_input_close(&in));
    else
        assert(cu_input_close(&in));
    cu_arena_release(&a);
    return 0;
}
