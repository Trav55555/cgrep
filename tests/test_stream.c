/* Streaming framing and lifecycle regressions. The source-inclusion
 * seam injects short reads and failures through the real CLI path. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cu.h"
#include "cgrep.h"

static int read_calls;
static int reserve_calls;
static int close_calls;
static size_t buffer_allocations;
static int fail_read_at;
static int fail_reserve_at;
static int fail_close_at;
static size_t read_limit;
static char directory[400];
static char output_text[4096];
static char error_text[4096];

static CuInputStatus stream_read(CuInput *in, void *buf, size_t cap, size_t *n)
{
    read_calls++;
    if (read_calls == fail_read_at) {
        in->state = CU_INPUT_ERROR;
        in->error = EDOM;
        *n = 0;
        return CU_INPUT_ERROR;
    }
    if (read_limit != 0 && cap > read_limit)
        cap = read_limit;
    return cu_input_read(in, buf, cap, n);
}

static int stream_reserve(CuVec *v, size_t extra)
{
    size_t before;
    int ok;

    reserve_calls++;
    if (reserve_calls == fail_reserve_at)
        cu_arena_fail_at(v->arena, v->arena->attempts);
    before = v->arena->attempts;
    ok = cu_vec_reserve(v, extra);
    buffer_allocations += v->arena->attempts - before;
    return ok;
}

static int stream_close(CuInput *in)
{
    int owned;
    int failed;
    int ok;

    owned = in->owns;
    failed = in->state == CU_INPUT_ERROR;
    ok = cu_input_close(in);
    close_calls++;
    if (close_calls == fail_close_at && owned && !failed) {
        in->state = CU_INPUT_ERROR;
        in->error = ERANGE;
        return 0;
    }
    return ok;
}

int cgrep_stream_probe(int argc, char **argv, FILE *in, FILE *out, FILE *err);

#define cu_input_read stream_read
#define cu_vec_reserve stream_reserve
#define cu_input_close stream_close
#define cgrep_main cgrep_stream_probe
#include "../src/cgrep.c"
#undef cu_input_read
#undef cu_vec_reserve
#undef cu_input_close
#undef cgrep_main

static void reset_faults(void)
{
    read_calls = 0;
    reserve_calls = 0;
    close_calls = 0;
    buffer_allocations = 0;
    fail_read_at = 0;
    fail_reserve_at = 0;
    fail_close_at = 0;
    read_limit = 0;
}

static void file_path(char *buf, const char *suffix)
{
    sprintf(buf, "%s/cgrep-stream-%s", directory, suffix);
}

static void write_file(const char *suffix, const char *data, size_t n)
{
    char filename[512];
    FILE *f;

    file_path(filename, suffix);
    f = fopen(filename, "wb");
    assert(f != NULL);
    assert(n == 0 || fwrite(data, 1, n, f) == n);
    assert(fclose(f) == 0);
}

static void capture(char *buf, size_t cap, const char *suffix)
{
    char filename[512];
    FILE *f;
    size_t n;

    file_path(filename, suffix);
    f = fopen(filename, "rb");
    assert(f != NULL);
    n = fread(buf, 1, cap - 1, f);
    assert(feof(f) && !ferror(f));
    assert(fclose(f) == 0);
    buf[n] = '\0';
}

static int run(int argc, const char *const *args, const char *data, size_t n)
{
    char *argv[8];
    char paths[8][512];
    char filename[512];
    FILE *in;
    FILE *out;
    FILE *err;
    int i;
    int result;

    assert(argc < 7);
    argv[0] = (char *)"cgrep";
    for (i = 0; i < argc; i++) {
        if (args[i][0] == '@') {
            file_path(paths[i], args[i] + 1);
            argv[i + 1] = paths[i];
        } else {
            argv[i + 1] = (char *)args[i];
        }
    }
    argv[argc + 1] = NULL;
    write_file("in", data, n);
    file_path(filename, "in");
    in = fopen(filename, "rb");
    file_path(filename, "out");
    out = fopen(filename, "wb");
    file_path(filename, "err");
    err = fopen(filename, "wb");
    assert(in != NULL && out != NULL && err != NULL);
    result = cgrep_stream_probe(argc + 1, argv, in, out, err);
    /* Reader detach must not close the caller's stream. */
    assert(fclose(in) == 0 && fclose(out) == 0 && fclose(err) == 0);
    capture(output_text, sizeof output_text, "out");
    capture(error_text, sizeof error_text, "err");
    return result;
}

static void test_short_reads(void)
{
    static const char *const args[] = {"-n", "alpha"};
    static const char input[] = "no\nunknown\nalpha\n\nalpha";
    size_t i;

    for (i = 1; i <= 7; i++) {
        reset_faults();
        read_limit = i;
        assert(run(2, args, input, sizeof input - 1) == 0);
        assert(strcmp(output_text, "3:alpha\n5:alpha\n") == 0);
        assert(error_text[0] == '\0' && close_calls == 1);
    }
}

static void test_boundaries(void)
{
    static const char *const args[] = {"-n", "alpha"};
    static const char *const utf8[] = {"\303\251"};
    char expected[64];
    char *bytes;
    size_t prefix_len;
    size_t n;

    bytes = (char *)malloc(STREAM_CHUNK * 2 + 16);
    assert(bytes != NULL);
    for (prefix_len = STREAM_CHUNK - 6; prefix_len <= STREAM_CHUNK + 2; prefix_len++) {
        memset(bytes, '\n', prefix_len);
        memcpy(bytes + prefix_len, "alpha\n", 6);
        reset_faults();
        assert(run(2, args, bytes, prefix_len + 6) == 0);
        sprintf(expected, "%lu:alpha\n", (unsigned long)prefix_len + 1);
        assert(strcmp(output_text, expected) == 0);
    }
    prefix_len = STREAM_CHUNK * 2 + 1;
    memset(bytes, '\n', prefix_len);
    memcpy(bytes + prefix_len, "alpha", 5);
    reset_faults();
    assert(run(2, args, bytes, prefix_len + 5) == 0);
    sprintf(expected, "%lu:alpha\n", (unsigned long)prefix_len + 1);
    assert(strcmp(output_text, expected) == 0);

    prefix_len = STREAM_CHUNK - 3;
    memset(bytes, '\n', prefix_len);
    memcpy(bytes + prefix_len, "alpha\r\n", 7);
    reset_faults();
    assert(run(2, args, bytes, prefix_len + 7) == 0);
    sprintf(expected, "%lu:alpha\r\n", (unsigned long)prefix_len + 1);
    assert(strcmp(output_text, expected) == 0);
    n = STREAM_CHUNK + 2;
    memset(bytes, '\n', n);
    memcpy(bytes + n, "\303\251\n", 3);
    reset_faults();
    assert(run(1, utf8, bytes, n + 3) == 0);
    assert(strcmp(output_text, "\303\251\n") == 0);
    free(bytes);
}

static void test_binary(void)
{
    static const char *const args[] = {"alpha"};
    static const char *const empty[] = {""};
    static const char *const one[] = {"a"};
    char *bytes;
    char expected[160];
    size_t n;

    bytes = (char *)malloc(STREAM_CHUNK * 2 + 16);
    assert(bytes != NULL);
    memcpy(bytes, "alpha\n", 6);
    memset(bytes + 6, '\n', STREAM_CHUNK - 6);
    bytes[STREAM_CHUNK] = '\0';
    memcpy(bytes + STREAM_CHUNK + 1, "tail\n", 5);
    reset_faults();
    assert(run(1, args, bytes, STREAM_CHUNK + 6) == 0);
    sprintf(expected, "alpha\nbinary file matches (found \"\\0\" byte around offset %lu)\n",
            (unsigned long)STREAM_CHUNK);
    assert(strcmp(output_text, expected) == 0);

    /* NUL in the first fill, only match split across the next fill. */
    memset(bytes, 'x', STREAM_CHUNK * 2);
    bytes[0] = '\0';
    memcpy(bytes + STREAM_CHUNK - 2, "alpha", 5);
    reset_faults();
    assert(run(1, args, bytes, STREAM_CHUNK + 3) == 0);
    assert(strcmp(output_text, "binary file matches (found \"\\0\" byte around offset 0)\n") == 0);
    reset_faults();
    read_limit = 1;
    assert(run(1, args, "\0alpha", 6) == 0);
    assert(strcmp(output_text, "binary file matches (found \"\\0\" byte around offset 0)\n") == 0);
    reset_faults();
    assert(run(1, empty, "\n\n\n\0", 4) == 0);
    assert(strcmp(output_text, "\n\n\nbinary file matches (found \"\\0\" byte around offset 3)\n") == 0);
    reset_faults();
    assert(run(1, empty, "\0", 1) == 0);
    assert(strcmp(output_text, "binary file matches (found \"\\0\" byte around offset 0)\n") == 0);
    reset_faults();
    assert(run(1, one, "\0a", 2) == 0);

    /* Save the first NUL's offset, not a later one. */
    n = STREAM_CHUNK - 1;
    memset(bytes, '\n', n);
    bytes[n] = '\0';
    bytes[n + 1] = '\0';
    memcpy(bytes + n + 2, "alpha", 5);
    reset_faults();
    assert(run(1, args, bytes, n + 7) == 0);
    sprintf(expected, "binary file matches (found \"\\0\" byte around offset %lu)\n", (unsigned long)n);
    assert(strcmp(output_text, expected) == 0);
    memset(bytes, 'x', STREAM_CHUNK * 2);
    bytes[0] = '\0';
    reset_faults();
    assert(run(1, args, bytes, STREAM_CHUNK * 2) == 1);
    assert(buffer_allocations == 1 && output_text[0] == '\0');
    free(bytes);
}

static void test_line_limit(void)
{
    static const char *const args[] = {"-n", "alpha"};
    static const char *const counted[] = {"-c", "alpha"};
    static const char *const listed[] = {"-l", "alpha"};
    static const char *const uncounted[] = {"-cv", "alpha"};
    static const char *const folded_args[] = {"-ni", "alpha"};
    static const char *const word_args[] = {"-nw", "alpha"};
    static const char *const inverted_args[] = {"-nv", "alpha"};
    char *bytes;
    int at;

    bytes = (char *)malloc(STREAM_LINE_LIMIT + 8);
    assert(bytes != NULL);
    memset(bytes, 'x', STREAM_LINE_LIMIT + 1);
    reset_faults();
    assert(run(2, args, bytes, STREAM_LINE_LIMIT - 1) == 1);
    assert(output_text[0] == '\0' && error_text[0] == '\0');
    reset_faults();
    assert(run(2, args, bytes, STREAM_LINE_LIMIT) == 1);
    assert(error_text[0] == '\0');
    /* A line too long to print is read through and skipped when it is
     * not selected; only a selected one is an error. */
    reset_faults();
    assert(run(2, args, bytes, STREAM_LINE_LIMIT + 1) == 1);
    assert(output_text[0] == '\0' && error_text[0] == '\0');
    bytes[STREAM_LINE_LIMIT] = '\n';
    memcpy(bytes + STREAM_LINE_LIMIT + 1, "alpha\n", 6);
    reset_faults();
    assert(run(2, args, bytes, STREAM_LINE_LIMIT + 7) == 0);
    assert(strcmp(output_text, "2:alpha\n") == 0);
    /* Twice the limit and more before the next line, which -i still prints
     * as read; a -w candidate inside the long line is not a word. */
    free(bytes);
    bytes = (char *)malloc(2 * STREAM_LINE_LIMIT + 16);
    assert(bytes != NULL);
    memset(bytes, 'x', 2 * STREAM_LINE_LIMIT);
    memcpy(bytes + 2 * STREAM_LINE_LIMIT, "\nALPHA\n", 7);
    reset_faults();
    assert(run(2, folded_args, bytes, 2 * STREAM_LINE_LIMIT + 7) == 0);
    assert(strcmp(output_text, "2:ALPHA\n") == 0 && error_text[0] == '\0');
    memcpy(bytes + STREAM_LINE_LIMIT + 3, "alpha", 5);
    reset_faults();
    assert(run(2, word_args, bytes, 2 * STREAM_LINE_LIMIT + 7) == 1);
    assert(output_text[0] == '\0' && error_text[0] == '\0');
    /* A match before or after the limit selects the line: an error. */
    reset_faults();
    assert(run(2, args, bytes, 2 * STREAM_LINE_LIMIT + 7) == 2);
    assert(strstr(error_text, "line exceeds 8 MiB") != NULL && output_text[0] == '\0');
    memcpy(bytes + STREAM_LINE_LIMIT + 3, "xxxxx", 5);
    memcpy(bytes + 16, "alpha", 5);
    reset_faults();
    assert(run(2, args, bytes, 2 * STREAM_LINE_LIMIT + 7) == 2);
    assert(strstr(error_text, "line exceeds 8 MiB") != NULL);
    /* -v selects a long line without a match; an unterminated one too. */
    memcpy(bytes + 16, "xxxxx", 5);
    reset_faults();
    assert(run(2, inverted_args, bytes, 2 * STREAM_LINE_LIMIT) == 2);
    assert(strstr(error_text, "line exceeds 8 MiB") != NULL);
    reset_faults();
    assert(run(2, args, bytes, 2 * STREAM_LINE_LIMIT) == 1);
    assert(output_text[0] == '\0' && error_text[0] == '\0');
    memcpy(bytes + 2 * STREAM_LINE_LIMIT - 5, "alpha", 5);
    reset_faults();
    assert(run(2, args, bytes, 2 * STREAM_LINE_LIMIT) == 2);
    assert(strstr(error_text, "line exceeds 8 MiB") != NULL);
    /* A NUL in the rest of a long named input keeps the line-limit
     * error; so does a read failure there, with its cause. */
    bytes[STREAM_LINE_LIMIT + 70000] = '\0';
    reset_faults();
    assert(run(2, args, bytes, 2 * STREAM_LINE_LIMIT) == 2);
    assert(strstr(error_text, "line exceeds 8 MiB") != NULL && output_text[0] == '\0');
    memset(bytes, 'x', 2 * STREAM_LINE_LIMIT);
    reset_faults();
    assert(run(2, args, bytes, 2 * STREAM_LINE_LIMIT) == 1);
    at = read_calls;
    reset_faults();
    fail_read_at = at - 1;
    assert(run(2, args, bytes, 2 * STREAM_LINE_LIMIT) == 2);
    assert(strstr(error_text, strerror(EDOM)) != NULL && output_text[0] == '\0');
    memset(bytes, 'x', STREAM_LINE_LIMIT + 1);
    bytes[STREAM_LINE_LIMIT] = '\n';
    memcpy(bytes + STREAM_LINE_LIMIT + 1, "alpha\n", 6);
    /* A NUL probe switches to binary, not the line-limit error. */
    memcpy(bytes, "alpha", 5);
    bytes[STREAM_LINE_LIMIT] = '\0';
    reset_faults();
    assert(run(2, args, bytes, STREAM_LINE_LIMIT + 1) == 0);
    assert(strstr(output_text, "binary file matches") != NULL && error_text[0] == '\0');
    /* -c, -l and -v -c keep no lines, so a line past the limit is counted
     * in one fixed buffer, as the reference does. */
    memset(bytes, 'x', STREAM_LINE_LIMIT + 1);
    memcpy(bytes + STREAM_LINE_LIMIT - 4, "alpha", 5);
    bytes[STREAM_LINE_LIMIT + 1] = '\n';
    reset_faults();
    assert(run(2, counted, bytes, STREAM_LINE_LIMIT + 2) == 0);
    assert(strcmp(output_text, "1\n") == 0 && error_text[0] == '\0' && buffer_allocations == 1);
    reset_faults();
    assert(run(2, listed, bytes, STREAM_LINE_LIMIT + 2) == 0);
    assert(strcmp(output_text, "<stdin>\n") == 0 && buffer_allocations == 1);
    reset_faults();
    assert(run(2, uncounted, bytes, STREAM_LINE_LIMIT + 2) == 1);
    assert(output_text[0] == '\0' && error_text[0] == '\0' && buffer_allocations == 1);
    free(bytes);
}

/* -w and -i in the streaming scan with short reads, so a candidate meets
 * every chunk boundary, including an undecided one whose next byte is in
 * the following read. Expected counts are the reference's. */
static void test_streamed_words(void)
{
    static const char *const word_count[] = {"-wc", "aa"};
    static const char *const word_invert[] = {"-wvc", "aa"};
    static const char *const word_list[] = {"-wl", "aa"};
    static const char *const empty_word[] = {"-wc", ""};
    static const char *const empty_invert[] = {"-wvc", ""};
    static const char *const folded[] = {"-ic", "AA"};
    static const char *const plain_count[] = {"-c", "ab"};
    /* Unicode neighbors (the reference's counts): U+3001 and an emoji are
     * not word characters; e-acute and a CJK ideograph are; an invalid
     * byte counts as one. Each is cut by every read size below. */
    static const char unicode[] = "x aa\343\200\201 y\naa\303\251\n\346\227\245aa\n"
                                  "\343\200\201aa\naa\360\237\230\200\naa\377\n";
    static const size_t limits[] = {1, 2, 3, 7};
    size_t i;

    for (i = 0; i < sizeof limits / sizeof limits[0]; i++) {
        reset_faults();
        read_limit = limits[i];
        assert(run(2, word_count, "aaa aa\nxaay\naa\n", 15) == 0);
        assert(strcmp(output_text, "2\n") == 0);
        reset_faults();
        read_limit = limits[i];
        assert(run(2, word_invert, "aaa aa\nxaay\naa\n", 15) == 0);
        assert(strcmp(output_text, "1\n") == 0);
        reset_faults();
        read_limit = limits[i];
        assert(run(2, word_list, "aaa\n", 4) == 1 && output_text[0] == '\0');
        reset_faults();
        read_limit = limits[i];
        assert(run(2, empty_word, "a\n\n-\nb", 7) == 0);
        assert(strcmp(output_text, "2\n") == 0);
        reset_faults();
        read_limit = limits[i];
        assert(run(2, empty_invert, "a\n\n-\nb", 7) == 0);
        assert(strcmp(output_text, "2\n") == 0);
        reset_faults();
        read_limit = limits[i];
        assert(run(2, folded, "aA b\nAa\nx\n", 10) == 0);
        assert(strcmp(output_text, "2\n") == 0);
        reset_faults();
        read_limit = limits[i];
        assert(run(2, word_count, unicode, sizeof unicode - 1) == 0);
        assert(strcmp(output_text, "3\n") == 0);
        reset_faults();
        read_limit = limits[i];
        assert(run(2, word_invert, unicode, sizeof unicode - 1) == 0);
        assert(strcmp(output_text, "3\n") == 0);
        reset_faults();
        read_limit = limits[i];
        assert(run(2, word_list, "aa\303\251\n", 5) == 1 && output_text[0] == '\0');
        /* Without -w a match may start at the carried first byte. */
        reset_faults();
        read_limit = limits[i];
        assert(run(2, plain_count, "xxab\nyyyab\n", 11) == 0);
        assert(strcmp(output_text, "2\n") == 0);
    }
}

static void test_failures(void)
{
    static const char *const files[] = {"alpha", "@first", "@second"};
    static const char *const one[] = {"a"};
    char *bytes;

    write_file("first", "alpha\n", 6);
    write_file("second", "alpha\n", 6);
    reset_faults();
    assert(run(3, files, "", 0) == 0);
    assert(buffer_allocations == 1 && close_calls == 2);
    reset_faults();
    fail_read_at = 2;
    assert(run(1, one, "a\nunfinished", 12) == 2);
    assert(strcmp(output_text, "a\n") == 0);
    assert(strstr(error_text, strerror(EDOM)) != NULL);
    reset_faults();
    fail_reserve_at = 1;
    fail_close_at = 1;
    assert(run(3, files, "", 0) == 2);
    assert(strstr(error_text, "out of memory") != NULL);
    assert(strstr(output_text, "second:alpha\n") != NULL && close_calls == 2);
    reset_faults();
    fail_close_at = 1;
    assert(run(3, files, "", 0) == 2);
    assert(strstr(error_text, "out of memory") == NULL && close_calls == 2);
    reset_faults();
    fail_read_at = 1;
    fail_close_at = 1;
    assert(run(3, files, "", 0) == 2);
    assert(strstr(output_text, "first:alpha") == NULL);
    assert(strstr(output_text, "second:alpha\n") != NULL);
    assert(strstr(error_text, strerror(EDOM)) != NULL);

    bytes = (char *)malloc(STREAM_CHUNK + 16);
    assert(bytes != NULL);
    memset(bytes, 'x', STREAM_CHUNK + 16);
    write_file("first", bytes, STREAM_CHUNK + 16);
    reset_faults();
    fail_reserve_at = 2;
    assert(run(3, files, "", 0) == 2);
    assert(strstr(error_text, "out of memory") != NULL);
    assert(strstr(output_text, "second:alpha\n") != NULL);
    free(bytes);
}

static void test_offsets(void)
{
    unsigned long total;

    total = ULONG_MAX - 1;
    assert(add_bytes(&total, 1) && total == ULONG_MAX);
    assert(!add_bytes(&total, 1) && total == ULONG_MAX);
    total = 0;
    assert(add_bytes(&total, 0) && total == 0);
}

int main(void)
{
    const char *tmp;

    tmp = getenv("TMPDIR");
    if (tmp == NULL || tmp[0] == '\0')
        tmp = "/tmp";
    assert(strlen(tmp) < sizeof directory);
    strcpy(directory, tmp);
    test_short_reads();
    test_boundaries();
    test_binary();
    test_line_limit();
    test_streamed_words();
    test_failures();
    test_offsets();
    return 0;
}
