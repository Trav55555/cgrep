/* Dense-word counting against a byte oracle, using exact-size allocations. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cu.h"
#include "cgrep.h"

int cgrep_count_probe(int argc, char **argv, FILE *in, FILE *out, FILE *err);
#define cgrep_main cgrep_count_probe
#include "../src/cgrep.c"
#undef cgrep_main

static unsigned long byte_count(const char *p, size_t n)
{
    unsigned long result;
    size_t i;

    result = 0;
    for (i = 0; i < n; i++) {
        if (p[i] == '\n')
            result++;
    }
    return result;
}

static void test_boundaries(void)
{
    static const size_t lengths[] = {0, 1, 3, 4, 7, 8, 9, 31, 255, 256, 257, 259, 260, 511, 513, 1025};
    static const unsigned char bytes[] = {'\n', '\v', '\v', '\n', 255, 128, 0, '\n'};
    char *storage;
    size_t k;
    size_t offset;
    size_t len;
    size_t size;
    size_t i;
    size_t cut;
    CuStr text;

    for (k = 0; k < sizeof lengths / sizeof lengths[0]; k++) {
        len = lengths[k];
        for (offset = 0; offset < 8; offset++) {
            size = offset + len;
            storage = (char *)malloc(size == 0 ? 1 : size);
            assert(storage != NULL);
            memset(storage, 'x', size == 0 ? 1 : size);
            for (i = 0; i < len; i++)
                storage[offset + i] = i < 4 ? '\n' : (char)bytes[(i - 4) % sizeof bytes];
            text = cu_str_n(storage, size);
            assert(count_lines(text, offset, size) == byte_count(storage + offset, len));
            for (cut = 0; cut <= len && cut < 8; cut++) {
                assert(count_lines(text, offset + cut, size) == byte_count(storage + offset + cut, len - cut));
                assert(count_lines(text, offset, size - cut) == byte_count(storage + offset, len - cut));
            }
            free(storage);
        }
    }
}

static void test_byte_values(void)
{
    char storage[4 + 64];
    unsigned value;

    memset(storage, '\n', 4);
    for (value = 0; value <= 255; value++) {
        memset(storage + 4, (int)value, 64);
        assert(count_lines(cu_str_n(storage, sizeof storage), 0, sizeof storage) ==
               byte_count(storage, sizeof storage));
    }
}

static void test_density_changes(void)
{
    char storage[1033];
    size_t i;
    unsigned long expected;

    memset(storage, '\n', sizeof storage);
    expected = sizeof storage;
    assert(count_lines(cu_str_n(storage, sizeof storage), 0, sizeof storage) == expected);
    memset(storage, 'x', sizeof storage);
    for (i = 0; i < sizeof storage; i += 257)
        storage[i] = '\n';
    assert(count_lines(cu_str_n(storage, sizeof storage), 0, sizeof storage) == byte_count(storage, sizeof storage));
    memset(storage, 'x', sizeof storage);
    memset(storage, '\n', 4);
    storage[sizeof storage - 1] = '\n';
    assert(count_lines(cu_str_n(storage, sizeof storage), 0, sizeof storage) == 5);
}

int main(void)
{
    test_boundaries();
    test_byte_values();
    test_density_changes();
    return 0;
}
