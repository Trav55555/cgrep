/* Whole-input reference: select complete lines by naive byte search, and
 * commit them only between deliveries. A NUL suppresses its entire delivery's
 * text; binary output waits for the first complete match in a delivered prefix.
 * No production finder, buffer, KMP state, or formatting helper is reused. */
#ifndef STREAM_ORACLE_H
#define STREAM_ORACLE_H

#define FRAME_INPUT_LIMIT 262144

static char reference_output[FRAME_INPUT_LIMIT * 16 + 256];
static size_t reference_length;

static size_t reference_find(const uint8_t *bytes, size_t len, const char *pattern, size_t n)
{
    size_t i;
    size_t j;

    if (n == 0)
        return 0;
    if (n > len)
        return (size_t)-1;
    for (i = 0; i <= len - n; i++) {
        for (j = 0; j < n && bytes[i + j] == (uint8_t)pattern[j]; j++) {}
        if (j == n)
            return i;
    }
    return (size_t)-1;
}

static void reference_append(const void *bytes, size_t n)
{
    assert(n <= sizeof reference_output - reference_length);
    memcpy(reference_output + reference_length, bytes, n);
    reference_length += n;
}

static int reference_line(const uint8_t *bytes, size_t begin, size_t end,
                          const char *pattern, size_t n, int names, int numbers,
                          unsigned long line)
{
    char label[32];
    int len;

    if (reference_find(bytes + begin, end - begin, pattern, n) == (size_t)-1)
        return 0;
    if (names)
        reference_append("input:", 6);
    if (numbers) {
        len = snprintf(label, sizeof label, "%lu:", line);
        assert(len > 0 && (size_t)len < sizeof label);
        reference_append(label, (size_t)len);
    }
    reference_append(bytes + begin, end - begin);
    reference_append("\n", 1);
    return 1;
}

static int reference_frame(const uint8_t *bytes, size_t len, const char *pattern, size_t n,
                           const size_t *deliveries, size_t count, int error,
                           int names, int numbers, int *stopped)
{
    size_t nul;
    size_t first_match;
    size_t scanned;
    size_t begin;
    size_t i;
    size_t k;
    unsigned long line;
    int matched;
    char warning[128];
    int warning_len;

    reference_length = 0;
    *stopped = 0;
    nul = 0;
    while (nul < len && bytes[nul] != 0)
        nul++;
    first_match = reference_find(bytes, len, pattern, n);
    scanned = 0;
    begin = 0;
    line = 1;
    matched = 0;
    for (k = 0; k < count; k++) {
        assert(deliveries[k] > scanned && deliveries[k] <= len);
        if (nul < deliveries[k]) {
            if (first_match != (size_t)-1 && first_match + n <= deliveries[k]) {
                if (names)
                    reference_append("input: ", 7);
                warning_len = snprintf(warning, sizeof warning,
                    "binary file matches (found \"\\0\" byte around offset %lu)\n",
                    (unsigned long)nul);
                assert(warning_len > 0 && (size_t)warning_len < sizeof warning);
                reference_append(warning, (size_t)warning_len);
                assert(k + 1 == count && !error);
                *stopped = 1;
                return 1;
            }
        } else {
            for (i = scanned; i < deliveries[k]; i++) {
                if (bytes[i] == '\n') {
                    matched |= reference_line(bytes, begin, i, pattern, n, names, numbers, line);
                    begin = i + 1;
                    line++;
                }
            }
        }
        scanned = deliveries[k];
    }
    if (!error) {
        assert(scanned == len);
        if (nul == len && begin < len)
            matched |= reference_line(bytes, begin, len, pattern, n, names, numbers, line);
    }
    return matched;
}

static void reference_check_output(FILE *file)
{
    char actual[1024];
    size_t at;
    size_t want;
    long end;

    assert(fflush(file) == 0 && !ferror(file));
    end = ftell(file);
    assert(end >= 0 && (size_t)end == reference_length);
    rewind(file);
    for (at = 0; at < reference_length; at += want) {
        want = reference_length - at;
        if (want > sizeof actual)
            want = sizeof actual;
        assert(fread(actual, 1, want, file) == want);
        assert(memcmp(actual, reference_output + at, want) == 0);
    }
}

#endif
