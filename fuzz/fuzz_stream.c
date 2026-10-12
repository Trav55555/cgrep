/* Memory-backed reader exercises the production framing loop with short
 * reads and injected errors. The output scratch file is reused at offset 0. */
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cu.h"
#include "cgrep.h"

static const uint8_t *input_bytes;
static size_t input_size;
static size_t input_pos;
static size_t step;
static size_t calls;
static size_t fail_at;
static int failed;
static FILE *sink;

static CuInputStatus memory_read(CuInput *in, void *buf, size_t cap, size_t *n)
{
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
    *n = cap;
    return CU_INPUT_DATA;
}

int cgrep_frame_probe(int argc, char **argv, FILE *in, FILE *out, FILE *err);

#define cu_input_read memory_read
#define cgrep_main cgrep_frame_probe
#include "../src/cgrep.c"
#undef cu_input_read
#undef cgrep_main

static int naive_contains(CuStr hay, CuStr needle)
{
    size_t i;
    size_t j;

    if (needle.len == 0)
        return hay.len != 0;
    if (needle.len > hay.len)
        return 0;
    for (i = 0; i <= hay.len - needle.len; i++) {
        j = 0;
        while (j < needle.len && hay.ptr[i + j] == needle.ptr[j])
            j++;
        if (j == needle.len)
            return 1;
    }
    return 0;
}

static void close_sink(void)
{
    if (sink != NULL)
        fclose(sink);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char pattern[32];
    CuArena a;
    CuVec buffer;
    CuInput in;
    Search searcher;
    ScanResult result;
    CuStr hay;
    size_t n;
    size_t i;

    if (size < 3 || size > 131072)
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
    step = (size_t)data[1] + 1;
    calls = 0;
    fail_at = data[2];
    failed = 0;
    hay = cu_str_n((const char *)input_bytes, input_size);
    cu_arena_init(&a);
    CU_VEC_INIT(&buffer, &a, char);
    assert(literal_init(&searcher.literal, &a, cu_str_n(pattern, n)));
    searcher.numbers = data[1] & 1;
    searcher.names = data[1] & 2;
    searcher.out = sink;
    cu_input_attach(&in, stdin);
    result = search_stream(&searcher, "input", &in, &buffer);
    if (failed) {
        assert(result.error == SCAN_IO && result.system_error == EDOM);
    } else {
        assert(result.error == SCAN_OK);
        assert(result.matched == naive_contains(hay, searcher.literal.text));
    }
    assert(fflush(sink) == 0 && !ferror(sink));
    if (failed)
        assert(!cu_input_close(&in));
    else
        assert(cu_input_close(&in));
    cu_arena_release(&a);
    return 0;
}
