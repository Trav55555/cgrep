/* Arbitrary byte strings, including NUL and high-bit bytes, checked
 * against naive search. This C99 harness includes the C89 finder. */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include "cu.h"
#include "cgrep.h"

int cgrep_fuzz_probe(int argc, char **argv, FILE *in, FILE *out, FILE *err);

#define cgrep_main cgrep_fuzz_probe
#include "../src/cgrep.c"
#undef cgrep_main

/* Streaming containment across chunks through find_end's KMP state. */
static int contains_chunk(const Literal *literal, CuStr chunk, size_t *state)
{
    if (literal->text.len == 0)
        return chunk.len != 0;
    if (literal->text.len == 1)
        return memchr(chunk.ptr, (unsigned char)literal->text.ptr[0], chunk.len) != NULL;
    return find_end(chunk, 0, literal, state) != NO_MATCH;
}

/* A fresh filter per call. */
static size_t find(CuStr hay, size_t start, const Literal *literal)
{
    Finder f;

    finder_init(&f, literal);
    return find_with(hay, start, literal, &f);
}

/* Unfiltered KMP from start; the oracle for the linear fallback. */
static size_t find_linear(CuStr hay, size_t start, const Literal *literal)
{
    size_t state;
    size_t end;

    state = 0;
    end = find_end(hay, start, literal, &state);
    return end == NO_MATCH ? NO_MATCH : end - literal->text.len;
}

static size_t naive(CuStr hay, size_t start, CuStr needle)
{
    size_t i;
    size_t j;

    if (start > hay.len || needle.len > hay.len - start)
        return (size_t)-1;
    if (needle.len == 0)
        return start;
    for (i = start; i <= hay.len - needle.len; i++) {
        j = 0;
        while (j < needle.len && hay.ptr[i + j] == needle.ptr[j])
            j++;
        if (j == needle.len)
            return i;
    }
    return (size_t)-1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    CuArena a;
    Literal literal;
    Finder finder;
    size_t at;
    CuStr hay;
    CuStr needle;
    size_t n;
    size_t start;
    size_t expected;
    size_t cut;
    size_t state;
    int found;

    if (size < 2 || size > 512)
        return 0;
    n = data[0] % (size - 1);
    needle = cu_str_n((const char *)data + 2, n);
    hay = cu_str_n((const char *)data + 2 + n, size - 2 - n);
    start = data[1] % (hay.len + 2);
    cu_arena_init(&a);
    assert(literal_init(&literal, &a, needle));
    expected = naive(hay, start, needle);
    assert(find(hay, start, &literal) == expected);
    if (needle.len >= 2)
        assert(find_linear(hay, start, &literal) == expected);
    /* One filter kept across every match, as search_text keeps it. */
    if (needle.len != 0) {
        finder_init(&finder, &literal);
        at = 0;
        while ((expected = naive(hay, at, needle)) != NO_MATCH) {
            assert(find_with(hay, at, &literal, &finder) == expected);
            at = expected + 1;
        }
        assert(find_with(hay, at, &literal, &finder) == NO_MATCH);
    }
    cut = data[1] % (hay.len + 1);
    state = 0;
    found = contains_chunk(&literal, cu_str_n(hay.ptr, cut), &state);
    if (!found)
        found = contains_chunk(&literal, cu_str_n(hay.ptr + cut, hay.len - cut), &state);
    assert(found == (needle.len == 0 ? hay.len != 0 : naive(hay, 0, needle) != NO_MATCH));
    cu_arena_release(&a);
    return 0;
}
