#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "cu.h"
#include "cgrep.h"

#define NO_MATCH ((size_t)-1)
#define STREAM_CHUNK ((size_t)65536)
#define STREAM_LINE_LIMIT ((size_t)8388608)

#ifndef CGREP_KMP_STEP
#define CGREP_KMP_STEP() ((void)0)
#endif

static const char usage[] = "usage: cgrep [-n] [-j N] PATTERN [PATH...]\n";

typedef struct {
    CuStr text;      /* borrowed from argv */
    size_t anchor;
    size_t alternate; /* same as anchor when all bytes are identical */
    size_t *prefix; /* arena-owned KMP table, NULL for lengths 0 and 1 */
} Literal;

typedef struct {
    Literal literal;
    int numbers; /* -n: prefix each line with its number */
    int names;   /* prefix each line with its input's name */
    FILE *out;
    int (*write)(void *context, const char *bytes, size_t n);
    int (*cancelled)(void *context);
    void *context;
} Search;

static int literal_init(Literal *literal, CuArena *a, CuStr text)
{
    size_t frequency[UCHAR_MAX + 1];
    size_t i;
    size_t matched;

    literal->text = text;
    literal->anchor = 0;
    literal->alternate = 0;
    literal->prefix = NULL;
    if (text.len < 2)
        return 1;
    literal->prefix = CU_PUSH(a, size_t, text.len);
    if (literal->prefix == NULL)
        return 0;
    for (i = 0; i < sizeof frequency / sizeof frequency[0]; i++)
        frequency[i] = 0;
    for (i = 0; i < text.len; i++)
        frequency[(unsigned char)text.ptr[i]]++;
    /* Pattern-local rarity is only a hint about the input. Prefer later
     * offsets on ties; bound false confirmations before switching to KMP. */
    for (i = 1; i < text.len; i++) {
        if (frequency[(unsigned char)text.ptr[i]] <=
            frequency[(unsigned char)text.ptr[literal->anchor]])
            literal->anchor = i;
    }
    literal->alternate = literal->anchor;
    for (i = 0; i < text.len; i++) {
        if (text.ptr[i] != text.ptr[literal->anchor] &&
            (literal->alternate == literal->anchor ||
             frequency[(unsigned char)text.ptr[i]] <=
             frequency[(unsigned char)text.ptr[literal->alternate]]))
            literal->alternate = i;
    }
    literal->prefix[0] = 0;
    matched = 0;
    for (i = 1; i < text.len; i++) {
        while (matched != 0 && text.ptr[i] != text.ptr[matched])
            matched = literal->prefix[matched - 1];
        if (text.ptr[i] == text.ptr[matched])
            matched++;
        literal->prefix[i] = matched;
    }
    return 1;
}

/* Exclusive end of the first match in this chunk, or NO_MATCH. A partial
 * prefix survives chunks without a match. Callers stop at the first match.
 * The literal has at least two bytes and *state is less than its length. */
static size_t find_end(CuStr hay, size_t start, const Literal *literal, size_t *state)
{
    size_t i;
    size_t matched;

    matched = *state;
    for (i = start; i < hay.len; i++) {
        CGREP_KMP_STEP();
        while (matched != 0 && hay.ptr[i] != literal->text.ptr[matched])
            matched = literal->prefix[matched - 1];
        if (hay.ptr[i] == literal->text.ptr[matched])
            matched++;
        if (matched == literal->text.len) {
            *state = literal->prefix[matched - 1];
            return i + 1;
        }
    }
    *state = matched;
    return NO_MATCH;
}

static size_t find_linear(CuStr hay, size_t start, const Literal *literal)
{
    size_t state;
    size_t end;

    state = 0;
    end = find_end(hay, start, literal, &state);
    return end == NO_MATCH ? NO_MATCH : end - literal->text.len;
}

static int contains_chunk(const Literal *literal, CuStr chunk, size_t *state)
{
    if (literal->text.len == 0)
        return chunk.len != 0;
    if (literal->text.len == 1)
        return memchr(chunk.ptr, (unsigned char)literal->text.ptr[0], chunk.len) != NULL;
    return find_end(chunk, 0, literal, state) != NO_MATCH;
}

/* Uniform patterns: every non-pattern byte blocks all starts through it.
 * Two failed full comparisons advance at least one pattern length; discovery
 * scans are capped at that length. Total search work remains linear. */
static size_t find_uniform(CuStr hay, size_t start, CuStr needle)
{
    size_t at;
    size_t last;
    size_t j;
    size_t span;
    const char *p;
    unsigned char byte;

    at = start;
    last = hay.len - needle.len;
    byte = (unsigned char)needle.ptr[0];
    while (at <= last) {
        if ((unsigned char)hay.ptr[at + needle.len - 1] != byte) {
            at += needle.len;
            continue;
        }
        if ((unsigned char)hay.ptr[at] != byte) {
            span = last - at + 1;
            if (span > needle.len)
                span = needle.len;
            p = (const char *)memchr(hay.ptr + at, byte, span);
            if (p == NULL)
                return NO_MATCH;
            at = (size_t)(p - hay.ptr);
            continue;
        }
        if (memcmp(hay.ptr + at, needle.ptr, needle.len) == 0)
            return at;
        j = needle.len - 1;
        while (j != 0 && (unsigned char)hay.ptr[at + j] == byte)
            j--;
        at += j + 1;
    }
    return NO_MATCH;
}

/* First occurrence at or after start. Inspect the first failed candidate
 * once to suggest an expected byte that actually mismatched. Each filter
 * gets at most eight false confirmations, then KMP bounds remaining work. */
static size_t find(CuStr hay, size_t start, const Literal *literal)
{
    CuStr needle;
    const char *p;
    size_t at;
    size_t last;
    unsigned rejects;
    size_t anchor;
    size_t suggested;
    size_t j;
    int switched;

    needle = literal->text;
    if (start > hay.len || needle.len > hay.len - start)
        return NO_MATCH;
    if (needle.len == 0)
        return start;
    if (needle.len == 1) {
        p = (const char *)memchr(hay.ptr + start, (unsigned char)needle.ptr[0], hay.len - start);
        return p == NULL ? NO_MATCH : (size_t)(p - hay.ptr);
    }
    if (literal->alternate == literal->anchor)
        return find_uniform(hay, start, needle);
    last = hay.len - needle.len;
    at = start;
    rejects = 0;
    anchor = literal->anchor;
    suggested = literal->alternate;
    switched = 0;
    while (at <= last) {
        p = (const char *)memchr(hay.ptr + at + anchor,
                                (unsigned char)needle.ptr[anchor], last - at + 1);
        if (p == NULL)
            return NO_MATCH;
        at = (size_t)(p - hay.ptr) - anchor;
        if (memcmp(hay.ptr + at, needle.ptr, needle.len) == 0)
            return at;
        if (rejects == 0 && !switched) {
            j = 0;
            while (j < needle.len && hay.ptr[at + j] == needle.ptr[j])
                j++;
            if (j < needle.len && needle.ptr[j] != needle.ptr[anchor])
                suggested = j;
        }
        at++;
        if (++rejects == 8) {
            if (switched || needle.ptr[anchor] == needle.ptr[suggested])
                return find_linear(hay, at, literal);
            anchor = suggested;
            switched = 1;
            rejects = 0;
        }
    }
    return NO_MATCH;
}

/* Count with bounded word batches only after repeated nearby newlines. */
static unsigned long count_lines(CuStr text, size_t from, size_t to)
{
    const char *p;
    const char *end;
    const char *found;
    const char *batch;
    unsigned long n;
    unsigned long word;
    unsigned long ones;
    unsigned long low;
    unsigned long high;
    unsigned long mask;
    size_t span;
    unsigned dense;
    int words;

    n = 0;
    dense = 0;
    p = text.ptr + from;
    end = text.ptr + to;
    /* Match both value width and object size before interpreting copied bytes.
     * Other representations retain the character-only path. */
    words = CHAR_BIT == 8 &&
        ((sizeof word == 4 && ULONG_MAX == 0xffffffffUL) ||
         (sizeof word == 8 && ULONG_MAX > 0xffffffffUL &&
          ULONG_MAX / 0xffffffffUL == 0xffffffffUL + 2UL));
    ones = ULONG_MAX / 255UL;
    low = ones * 127UL;
    high = ones * 128UL;
    while (p < end && (found = (const char *)memchr(p, '\n', (size_t)(end - p))) != NULL) {
        n++;
        dense = found - p < 64 ? (dense < 4 ? dense + 1 : 4) : 0;
        p = found + 1;
        if (words && dense == 4) {
            span = (size_t)(end - p);
            if (span > 256)
                span = 256;
            batch = p + span;
            while ((size_t)(batch - p) >= sizeof word) {
                memcpy(&word, p, sizeof word);
                word ^= ones * (unsigned long)'\n';
                /* No carry escapes a byte's low seven bits. Unlike a borrow-
                 * based has-zero test, adjacent 0/1 bytes cannot overcount.
                 * Unsigned multiplication collects the lane flags modulo the
                 * word width; the highest byte holds their sum (at most 8). */
                mask = ~(((word & low) + low) | word | low) & high;
                n += ((mask >> 7) * ones) >> ((sizeof word - 1) * CHAR_BIT);
                p += sizeof word;
            }
            while (p < batch) {
                n += *p == '\n';
                p++;
            }
            dense = 0;
        }
    }
    return n;
}

/* Search a complete-line window, or the final unterminated tail. *lines
 * counts LF bytes before this window. -1 means an observed output error. */
static int search_text(const Search *s, const char *name, CuStr text, unsigned long *lines)
{
    size_t pos;
    size_t m;
    size_t start;
    size_t end;
    size_t counted;
    unsigned long seen;
    int matched;

    matched = 0;
    pos = 0;
    counted = 0;
    seen = *lines;
    while (pos < text.len && (m = find(text, pos, &s->literal)) != NO_MATCH) {
        /* pos is always a line's start, so the match's line starts there
         * or after it. */
        start = m;
        while (start > pos && text.ptr[start - 1] != '\n')
            start--;
        end = m;
        while (end < text.len && text.ptr[end] != '\n')
            end++;
        if (s->numbers) {
            seen += count_lines(text, counted, start);
            counted = start;
        }
        if (s->write != NULL) {
            char number[sizeof(unsigned long) * CHAR_BIT + 2];
            int digits;

            if (s->names &&
                (!s->write(s->context, name, strlen(name)) || !s->write(s->context, ":", 1)))
                return -1;
            if (s->numbers) {
                digits = sprintf(number, "%lu:", seen + 1);
                if (!s->write(s->context, number, (size_t)digits))
                    return -1;
            }
            if (!s->write(s->context, text.ptr + start, end - start) || !s->write(s->context, "\n", 1))
                return -1;
        } else {
            if (s->names)
                fprintf(s->out, "%s:", name);
            if (s->numbers)
                fprintf(s->out, "%lu:", seen + 1);
            fwrite(text.ptr + start, 1, end - start, s->out);
            fputc('\n', s->out);
            if (ferror(s->out))
                return -1;
        }
        matched = 1;
        if (end == text.len)
            break;
        pos = end + 1;
    }
    if (s->numbers)
        *lines = seen + count_lines(text, counted, text.len);
    return matched;
}

typedef enum {
    SCAN_OK,
    SCAN_IO,
    SCAN_MEMORY,
    SCAN_LINE_LIMIT,
    SCAN_RANGE,
    SCAN_OUTPUT,
    SCAN_CANCELLED
} ScanError;

typedef struct {
    int matched;
    ScanError error;
    int system_error;
} ScanResult;

static int add_bytes(unsigned long *total, size_t n)
{
    if (n > ULONG_MAX - *total)
        return 0;
    *total += (unsigned long)n;
    return 1;
}

static int binary_warning(const Search *s, const char *name, unsigned long offset)
{
    if (s->write != NULL) {
        char warning[sizeof(unsigned long) * CHAR_BIT + 64];
        int n;

        if (s->names &&
            (!s->write(s->context, name, strlen(name)) || !s->write(s->context, ": ", 2)))
            return -1;
        n = sprintf(warning, "binary file matches (found \"\\0\" byte around offset %lu)\n", offset);
        return s->write(s->context, warning, (size_t)n) ? 1 : -1;
    }
    if (s->names)
        fprintf(s->out, "%s: ", name);
    fprintf(s->out, "binary file matches (found \"\\0\" byte around offset %lu)\n", offset);
    return ferror(s->out) ? -1 : 1;
}

static ScanResult search_stream(const Search *s, const char *name, CuInput *in, CuVec *buffer)
{
    ScanResult result;
    CuInputStatus status;
    CuArena *arena;
    const char *nul;
    char *destination;
    char extra;
    size_t want;
    size_t got;
    size_t old_len;
    size_t end;
    size_t prefix;
    unsigned long total;
    unsigned long read_base;
    unsigned long lines;
    unsigned long nul_offset;
    int binary;
    int probing;
    int found;

    result.matched = 0;
    result.error = SCAN_OK;
    result.system_error = 0;
    total = 0;
    lines = 0;
    nul_offset = 0;
    prefix = 0;
    binary = 0;
    /* A failed growth must not poison later inputs. Successful storage is
     * reused across files; only failed vectors discard their arena. */
    if (cu_vec_failed(buffer)) {
        arena = buffer->arena;
        cu_arena_release(arena);
        CU_VEC_INIT(buffer, arena, char);
    }
    cu_vec_clear(buffer);
    if (!cu_vec_reserve(buffer, STREAM_CHUNK)) {
        result.error = SCAN_MEMORY;
        return result;
    }
    for (;;) {
        if (s->cancelled != NULL && s->cancelled(s->context)) {
            result.error = SCAN_CANCELLED;
            break;
        }
        if (buffer->len == buffer->cap && buffer->cap < STREAM_LINE_LIMIT) {
            if (!cu_vec_reserve(buffer, 1)) {
                result.error = SCAN_MEMORY;
                break;
            }
        }
        old_len = buffer->len;
        probing = old_len == buffer->cap;
        destination = probing ? &extra : buffer->data + old_len;
        want = probing ? 1 : buffer->cap - old_len;
        if (want > STREAM_CHUNK)
            want = STREAM_CHUNK;
        /* Match the streaming reference's initial encoding-probe boundary.
         * Bytes stay raw here: cgrep does not decode or strip a BOM. */
        if (total == 0 && want > 3)
            want = 3;
        status = cu_input_read(in, destination, want, &got);
        if (status == CU_INPUT_ERROR) {
            result.error = SCAN_IO;
            result.system_error = cu_input_errno(in);
            break;
        }
        if (status == CU_INPUT_END) {
            if (!binary && old_len != 0) {
                found = search_text(s, name, cu_str_n(buffer->data, old_len), &lines);
                if (found < 0)
                    result.error = SCAN_OUTPUT;
                else
                    result.matched |= found;
            }
            break;
        }
        read_base = total;
        if (!add_bytes(&total, got)) {
            result.error = SCAN_RANGE;
            break;
        }
        nul = binary ? NULL : (const char *)memchr(destination, '\0', got);
        if (nul != NULL) {
            nul_offset = read_base + (unsigned long)(nul - destination);
            binary = 1;
            if (result.matched ||
                contains_chunk(&s->literal, cu_str_n(buffer->data, old_len), &prefix)) {
                found = binary_warning(s, name, nul_offset);
                result.matched = 1;
                if (found < 0)
                    result.error = SCAN_OUTPUT;
                break;
            }
        }
        if (binary) {
            if (contains_chunk(&s->literal, cu_str_n(destination, got), &prefix)) {
                found = binary_warning(s, name, nul_offset);
                result.matched = 1;
                if (found < 0)
                    result.error = SCAN_OUTPUT;
                break;
            }
            cu_vec_clear(buffer);
            continue;
        }
        if (probing) {
            if (extra != '\n') {
                result.error = SCAN_LINE_LIMIT;
                break;
            }
            found = search_text(s, name, cu_str_n(buffer->data, old_len), &lines);
            if (found < 0) {
                result.error = SCAN_OUTPUT;
                break;
            }
            result.matched |= found;
            if (s->numbers)
                lines++;
            cu_vec_clear(buffer);
            continue;
        }
        buffer->len += got;
        /* Old bytes contain no LF. Search only new bytes for the last LF,
         * so an unfinished long line is neither rescanned nor recopied. */
        end = buffer->len;
        while (end > old_len && buffer->data[end - 1] != '\n')
            end--;
        if (end == old_len)
            continue;
        found = search_text(s, name, cu_str_n(buffer->data, end), &lines);
        if (found < 0) {
            result.error = SCAN_OUTPUT;
            break;
        }
        result.matched |= found;
        buffer->len -= end;
        if (buffer->len != 0)
            memmove(buffer->data, buffer->data + end, buffer->len);
    }
    return result;
}

static const char *scan_error(ScanResult result)
{
    switch (result.error) {
    case SCAN_IO:
        return result.system_error == 0 ? "input/output error" : strerror(result.system_error);
    case SCAN_MEMORY:
        return "out of memory";
    case SCAN_LINE_LIMIT:
        return "line exceeds 8 MiB limit";
    case SCAN_RANGE:
        return "input exceeds byte-offset range";
    case SCAN_OUTPUT:
        return "output error";
    case SCAN_CANCELLED:
        return "cancelled";
    case SCAN_OK:
        break;
    }
    return "unknown error";
}

/* Owned files close here; attached stdin remains open at its consumed
 * position. Keep the first failure even if close also fails. */
static int search_input(const Search *s, const char *path, FILE *in, FILE *err, CuVec *buffer)
{
    CuInput input;
    ScanResult result;
    const char *name;
    int closed;

    name = path == NULL || strcmp(path, "-") == 0 ? "<stdin>" : path;
    if (name == path) {
        if (!cu_input_open(&input, path)) {
            result.matched = 0;
            result.error = SCAN_IO;
            result.system_error = cu_input_errno(&input);
            fprintf(err, "cgrep: %s: %s\n", name, scan_error(result));
            return -1;
        }
    } else {
        cu_input_attach(&input, in);
    }
    result = search_stream(s, name, &input, buffer);
    closed = cu_input_close(&input);
    if (!closed && result.error == SCAN_OK) {
        result.error = SCAN_IO;
        result.system_error = cu_input_errno(&input);
    }
    if (result.error != SCAN_OK) {
        fprintf(err, "cgrep: %s: %s\n", name, scan_error(result));
        return -1;
    }
    return result.matched;
}

#include "parallel.inc"

static int worker_count(const char *text)
{
    int n;

    n = 0;
    if (*text == '\0')
        return 0;
    while (*text != '\0') {
        if (*text < '0' || *text > '9' || n > 8)
            return 0;
        n = n * 10 + (*text++ - '0');
    }
    return n >= 1 && n <= 8 ? n : 0;
}

int cgrep_main(int argc, char **argv, FILE *in, FILE *out, FILE *err)
{
    Search s;
    CuArena pattern_arena;
    CuArena input_arena;
    CuVec buffer;
    CuStr pattern;
    int i;
    int r;
    int matched;
    int failed;
    int jobs;

    s.numbers = 0;
    s.out = out;
    s.write = NULL;
    s.cancelled = NULL;
    s.context = NULL;
    jobs = 1;
    i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        if (strncmp(argv[i], "-j", 2) == 0) {
            if (argv[i][2] != '\0')
                jobs = worker_count(argv[i] + 2);
            else if (i + 1 < argc)
                jobs = worker_count(argv[++i]);
            else
                jobs = 0;
            if (jobs == 0) {
                fputs("cgrep: worker count must be 1 through 8\n", err);
                return 2;
            }
            i++;
            continue;
        }
        if (strcmp(argv[i], "-n") != 0) {
            fprintf(err, "cgrep: unknown flag %s\n%s", argv[i], usage);
            return 2;
        }
        s.numbers = 1;
        i++;
    }
    if (i >= argc) {
        fputs(usage, err);
        return 2;
    }
    pattern = cu_str_cstr(argv[i++]);
    if (memchr(pattern.ptr, '\n', pattern.len) != NULL) {
        fputs("cgrep: a pattern cannot contain a newline\n", err);
        return 2;
    }
    cu_arena_init(&pattern_arena);
    if (!literal_init(&s.literal, &pattern_arena, pattern)) {
        fputs("cgrep: out of memory preparing pattern\n", err);
        cu_arena_release(&pattern_arena);
        return 2;
    }
    cu_arena_init(&input_arena);
    CU_VEC_INIT(&buffer, &input_arena, char);
    s.names = argc - i > 1;
    if (jobs > 1 && argc - i > 1 &&
        parallel_search(&s, argv + i, argc - i, jobs, in, out, err, &r)) {
        cu_arena_release(&input_arena);
        cu_arena_release(&pattern_arena);
        return r;
    }
    matched = 0;
    failed = 0;
    if (i == argc) {
        r = search_input(&s, NULL, in, err, &buffer);
        matched = r == 1;
        failed = r < 0;
    }
    for (; i < argc; i++) {
        r = search_input(&s, argv[i], in, err, &buffer);
        if (r == 1)
            matched = 1;
        else if (r < 0)
            failed = 1;
        if (fflush(out) != 0 || ferror(out)) {
            fputs("cgrep: output error\n", err);
            failed = 1;
            break;
        }
    }
    if (fflush(out) != 0 || ferror(out))
        failed = 1;
    cu_arena_release(&input_arena);
    cu_arena_release(&pattern_arena);
    return failed ? 2 : matched ? 0 : 1;
}
