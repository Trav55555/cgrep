/* C99 replay driver, outside the C89 unit-test glob. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "../../fuzz/fuzz_stream.c"

static void known_output(const uint8_t *bytes, size_t len, const char *pattern,
                         const size_t *ends, size_t count, int error,
                         int names, int numbers, int matched, int stops,
                         const char *expected)
{
    int stopped;

    assert(reference_frame(bytes, len, pattern, strlen(pattern), ends, count,
        error, names, numbers, &stopped) == matched);
    assert(stopped == stops);
    assert(reference_length == strlen(expected));
    assert(memcmp(reference_output, expected, reference_length) == 0);
}

static void test_reference(void)
{
    static const uint8_t text[] = "no\nalpha\nalpha";
    static const size_t text_ends[] = {3, 9, 14};
    static const uint8_t binary[] = "alpha\n\0tail";
    static const size_t one_delivery[] = {10};
    static const size_t two_deliveries[] = {6, 10};
    static const uint8_t partial[] = "a\nunfinished";
    static const size_t partial_ends[] = {3};
    static const uint8_t split[] = "\0alpha";
    static const size_t split_ends[] = {1, 3, 6};
    static const uint8_t empty_lines[] = "\n\n";
    static const size_t empty_ends[] = {2};

    known_output(text, sizeof text - 1, "alpha", text_ends, 3, 0, 1, 1, 1, 0,
        "input:2:alpha\ninput:3:alpha\n");
    known_output(binary, sizeof binary - 1, "alpha", one_delivery, 1, 0, 1, 1, 1, 1,
        "input: binary file matches (found \"\\0\" byte around offset 6)\n");
    known_output(binary, sizeof binary - 1, "alpha", two_deliveries, 2, 0, 1, 1, 1, 1,
        "input:1:alpha\ninput: binary file matches (found \"\\0\" byte around offset 6)\n");
    known_output(partial, sizeof partial - 1, "a", partial_ends, 1, 1, 0, 0, 1, 0, "a\n");
    known_output(split, sizeof split - 1, "alpha", split_ends, 3, 0, 0, 0, 1, 1,
        "binary file matches (found \"\\0\" byte around offset 0)\n");
    known_output(empty_lines, sizeof empty_lines - 1, "", empty_ends, 1, 0, 0, 0, 1, 0, "\n\n");
    known_output(text, 0, "", text_ends, 0, 0, 0, 0, 0, 0, "");
}

int main(int argc, char **argv)
{
    static uint8_t data[FRAME_INPUT_LIMIT];
    FILE *f;
    size_t size;
    int i;

    test_reference();
    for (i = 1; i < argc; i++) {
        f = fopen(argv[i], "rb");
        assert(f != NULL);
        size = fread(data, 1, sizeof data, f);
        assert(!ferror(f) && fgetc(f) == EOF);
        assert(fclose(f) == 0);
        LLVMFuzzerTestOneInput(data, size);
    }
    printf("framing replay: PASS (%d fixtures plus known-output oracle tests)\n", argc - 1);
    return 0;
}
