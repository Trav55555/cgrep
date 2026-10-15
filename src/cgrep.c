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

/* SSE2 is baseline on x86-64, so no runtime dispatch is needed. Other
 * targets, tcc, Cerberus and CGREP_NO_SIMD builds use the portable code.
 * GCC/Clang vector types stand in for <emmintrin.h>, whose definitions
 * the project lint would otherwise check. */
#if defined(__GNUC__) && defined(__x86_64__) && defined(__SSE2__) && \
    !defined(__cerb__) && !defined(CGREP_NO_SIMD)
#define CGREP_SSE2 1
typedef char Bytes16 __attribute__((vector_size(16)));

static Bytes16 load16(const char *p)
{
    Bytes16 v;

    memcpy(&v, p, sizeof v);
    return v;
}

static Bytes16 splat16(char c)
{
    char b[16];

    memset(b, c, sizeof b);
    return load16(b);
}

/* One bit per byte lane equal in both comparisons (pmovmskb). */
static unsigned both_equal(Bytes16 a, Bytes16 b, Bytes16 c, Bytes16 d)
{
    return (unsigned)__builtin_ia32_pmovmskb128((Bytes16)(a == b) & (Bytes16)(c == d));
}
#endif

/* KMP scans at most this many bytes beyond twice the pattern length
 * before the candidate filter resumes. */
#define KMP_WINDOW ((size_t)4096)

/* make passes the cgrep and c-utils revisions; other builds lack them. */
#ifndef CGREP_REVISION
#define CGREP_REVISION "unknown"
#endif

static const char usage[] = "usage: cgrep [-n] [-j N] PATTERN [PATH...]\n"
                            "       cgrep --version\n";

typedef struct {
    CuStr text;      /* borrowed from argv */
    size_t anchor;   /* rarest byte by background rank */
    size_t alternate; /* rarest other byte value; same as anchor when uniform */
    size_t *prefix; /* arena-owned KMP table, NULL for lengths 0 and 1 */
} Literal;

/* Mutable filter choice for one input. A candidate must match the pattern
 * at both offsets. One learned switch persists across calls and windows,
 * as does the vector pair filter once memchr hits prove dense. */
typedef struct {
    size_t first;
    size_t second;
    int switched;
    int vector;
} Finder;

typedef struct {
    Literal literal;
    int numbers; /* -n: prefix each line with its number */
    int names;   /* prefix each line with its input's name */
    FILE *out;
    /* Borrowed fragments are consumed synchronously; zero stops scanning. */
    int (*write)(void *context, const CuStr *parts, size_t count);
    int (*cancelled)(void *context);
    void *context;
    Finder *finder; /* per-input filter state; NULL uses a fresh one per window */
} Search;

/* Background byte frequency ranks: higher is more common. From the memchr
 * crate's default_rank.rs (Unlicense), built from a mixed text/code/binary
 * corpus. The input's own distribution is unknown before searching. */
static const unsigned char byte_rank[UCHAR_MAX + 1] = {
    55, 52, 51, 50, 49, 48, 47, 46, 45, 103, 242, 66, 67, 229, 44, 43,
    42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 56, 32, 31, 30, 29, 28,
    255, 148, 164, 149, 136, 160, 155, 173, 221, 222, 134, 122, 232, 202, 215, 224,
    208, 220, 204, 187, 183, 179, 177, 168, 178, 200, 226, 195, 154, 184, 174, 126,
    120, 191, 157, 194, 170, 189, 162, 161, 150, 193, 142, 137, 171, 176, 185, 167,
    186, 112, 175, 192, 188, 156, 140, 143, 123, 133, 128, 147, 138, 146, 114, 223,
    151, 249, 216, 238, 236, 253, 227, 218, 230, 247, 135, 180, 241, 233, 246, 244,
    231, 139, 245, 243, 251, 235, 201, 196, 240, 214, 152, 182, 205, 181, 127, 27,
    212, 211, 210, 213, 228, 197, 169, 159, 131, 172, 105, 80, 98, 96, 97, 81,
    207, 145, 116, 115, 144, 130, 153, 121, 107, 132, 109, 110, 124, 111, 82, 108,
    118, 141, 113, 129, 119, 125, 165, 117, 92, 106, 83, 72, 99, 93, 65, 79,
    166, 237, 163, 199, 190, 225, 209, 203, 198, 217, 219, 206, 234, 248, 158, 239,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
    255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
};

static unsigned rank_at(CuStr text, size_t i)
{
    return byte_rank[(unsigned char)text.ptr[i]];
}

static int literal_init(Literal *literal, CuArena *a, CuStr text)
{
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
    /* Background rank is only a hint about the input; search corrects a
     * hot choice once. Prefer later offsets on ties. */
    for (i = 1; i < text.len; i++) {
        if (rank_at(text, i) <= rank_at(text, literal->anchor))
            literal->anchor = i;
    }
    literal->alternate = literal->anchor;
    for (i = 0; i < text.len; i++) {
        if (text.ptr[i] != text.ptr[literal->anchor] &&
            (literal->alternate == literal->anchor ||
             rank_at(text, i) <= rank_at(text, literal->alternate)))
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

static void finder_init(Finder *f, const Literal *literal)
{
    f->first = literal->anchor;
    f->second = literal->alternate;
    f->switched = 0;
    f->vector = 0;
}

/* First start in [at, last] whose first filter byte matches, or NO_MATCH.
 * The caller checks the second byte. libc memchr is fastest while the
 * first byte is actually rare in the input. */
static size_t first_candidate(CuStr hay, size_t at, size_t last, CuStr needle, const Finder *f)
{
    const char *p;

    p = (const char *)memchr(hay.ptr + at + f->first,
                             (unsigned char)needle.ptr[f->first], last - at + 1);
    return p == NULL ? NO_MATCH : (size_t)(p - hay.ptr) - f->first;
}

#ifdef CGREP_SSE2
/* First start in [at, last] whose bytes at both filter offsets match, or
 * NO_MATCH. Loads stay inside hay; the tail is checked bytewise. */
static size_t pair_candidate(CuStr hay, size_t at, size_t last, CuStr needle, const Finder *f)
{
    const char *p1;
    const char *p2;
    Bytes16 b1;
    Bytes16 b2;
    size_t reach;
    unsigned mask;

    reach = f->first > f->second ? f->first : f->second;
    p1 = hay.ptr + f->first;
    p2 = hay.ptr + f->second;
    b1 = splat16(needle.ptr[f->first]);
    b2 = splat16(needle.ptr[f->second]);
    while (at <= last && hay.len - at >= reach + 16) {
        mask = both_equal(load16(p1 + at), b1, load16(p2 + at), b2);
        if (mask != 0) {
            at += (size_t)__builtin_ctz(mask);
            return at <= last ? at : NO_MATCH;
        }
        at += 16;
    }
    for (; at <= last; at++) {
        if (p1[at] == needle.ptr[f->first] && p2[at] == needle.ptr[f->second])
            return at;
    }
    return NO_MATCH;
}

static size_t candidate(CuStr hay, size_t at, size_t last, CuStr needle, const Finder *f)
{
    return f->vector ? pair_candidate(hay, at, last, needle, f) : first_candidate(hay, at, last, needle, f);
}
#else
#define candidate first_candidate
#endif

/* First occurrence at or after start. Candidates cost a fixed charge plus
 * the pattern length when confirmed. When that work exceeds the bytes
 * advanced plus slack, the filter switches once to a byte that actually
 * mismatched; later, KMP scans one bounded window and the filter resumes.
 * Each phase's excess is O(pattern length) and each window advances past
 * at least KMP_WINDOW bytes, so total work stays O(input + pattern). */
static size_t find_with(CuStr hay, size_t start, const Literal *literal, Finder *f)
{
    CuStr needle;
    const char *p;
    size_t at;
    size_t last;
    size_t base;
    size_t spent;
    size_t slack;
    size_t suggested;
    size_t window;
    size_t state;
    size_t end;
    size_t j;
    size_t hits;

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
    slack = 2 * needle.len + 64;
    at = start;
    base = at;
    spent = 0;
    hits = 0;
    suggested = NO_MATCH;
    while (at <= last) {
        at = candidate(hay, at, last, needle, f);
        if (at == NO_MATCH)
            return NO_MATCH;
#ifdef CGREP_SSE2
        /* Dense first-byte hits cost more per call than a vector scan. */
        if (!f->vector && ++hits >= 8 && hits * 64 > at - base)
            f->vector = 1;
#else
        (void)hits;
#endif
        if (hay.ptr[at + f->second] != needle.ptr[f->second]) {
            /* memchr filter only: the first byte is hot here. */
            if (suggested == NO_MATCH)
                suggested = f->second;
        } else {
            if (memcmp(hay.ptr + at, needle.ptr, needle.len) == 0)
                return at;
            spent += needle.len;
            if (suggested == NO_MATCH) {
                j = 0;
                while (j < needle.len && hay.ptr[at + j] == needle.ptr[j])
                    j++;
                if (j < needle.len && needle.ptr[j] != needle.ptr[f->first])
                    suggested = j;
            }
        }
        spent += 8;
        at++;
        if (spent <= at - base + slack)
            continue;
        if (!f->switched && suggested != NO_MATCH &&
            needle.ptr[suggested] != needle.ptr[f->first]) {
            f->second = f->first;
            f->first = suggested;
            f->switched = 1;
        } else if (at <= last) {
            window = hay.len - at;
            if (window > 2 * needle.len + KMP_WINDOW)
                window = 2 * needle.len + KMP_WINDOW;
            state = 0;
            end = find_end(cu_str_n(hay.ptr, at + window), at, literal, &state);
            if (end != NO_MATCH)
                return end - needle.len;
            /* Every start before this one ends inside the scanned window. */
            at = at + window - needle.len + 1;
        }
        base = at;
        spent = 0;
        hits = 0;
        suggested = NO_MATCH;
    }
    return NO_MATCH;
}

#ifdef CGREP_SSE2
/* Byte lanes subtract compare masks (-1 per LF); flushing every 255
 * blocks keeps each lane below 256. */
static unsigned long count_lines(CuStr text, size_t from, size_t to)
{
    const char *p;
    const char *end;
    Bytes16 newline;
    Bytes16 lanes;
    unsigned char sums[16];
    size_t blocks;
    size_t i;
    unsigned long n;

    n = 0;
    p = text.ptr + from;
    end = text.ptr + to;
    newline = splat16('\n');
    while ((size_t)(end - p) >= 16) {
        blocks = (size_t)(end - p) / 16;
        if (blocks > 255)
            blocks = 255;
        lanes = newline ^ newline;
        while (blocks-- != 0) {
            lanes -= (Bytes16)(load16(p) == newline);
            p += 16;
        }
        memcpy(sums, &lanes, sizeof sums);
        for (i = 0; i < sizeof sums; i++)
            n += sums[i];
    }
    while (p < end)
        n += *p++ == '\n';
    return n;
}
#else
/* One flag byte per LF, with no cross-byte borrow propagation. */
static unsigned long lf_flags(unsigned long word, unsigned long ones,
                              unsigned long low, unsigned long high)
{
    word ^= ones * (unsigned long)'\n';
    return (~(((word & low) + low) | word | low) & high) >> 7;
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
    unsigned long w1;
    unsigned long w2;
    unsigned long w3;
    unsigned long lanes;
    const char *word_end;
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
            if (span > 1024)
                span = 1024;
            batch = p + span;
            while ((size_t)(batch - p) >= sizeof word) {
                span = (size_t)(batch - p) / sizeof word;
                if (span > 16)
                    span = 16;
                word_end = p + span * sizeof word;
                /* At most 16 copied words: each byte lane sums to <=16,
                 * and the final byte sum is <=128, never wrapping at 256. */
                lanes = 0;
                while ((size_t)(word_end - p) >= 4 * sizeof word) {
                    memcpy(&word, p, sizeof word);
                    memcpy(&w1, p + sizeof word, sizeof w1);
                    memcpy(&w2, p + 2 * sizeof word, sizeof w2);
                    memcpy(&w3, p + 3 * sizeof word, sizeof w3);
                    lanes += lf_flags(word, ones, low, high) + lf_flags(w1, ones, low, high) +
                             lf_flags(w2, ones, low, high) + lf_flags(w3, ones, low, high);
                    p += 4 * sizeof word;
                }
                while (p < word_end) {
                    memcpy(&word, p, sizeof word);
                    lanes += lf_flags(word, ones, low, high);
                    p += sizeof word;
                }
                n += (lanes * ones) >> ((sizeof word - 1) * CHAR_BIT);
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
#endif

static CuStr number_prefix(char *end, unsigned long value)
{
    char *p;

    p = end;
    *--p = ':';
    do {
        *--p = (char)('0' + value % 10UL);
        value /= 10UL;
    } while (value != 0);
    return cu_str_n(p, (size_t)(end - p));
}

/* glibc's unlocked stdio skips per-call stream locking. Only the thread
 * that owns the CLI stream writes to it. */
#if defined(__GLIBC__) && defined(_GNU_SOURCE) && !defined(__cerb__)
#define CGREP_FWRITE fwrite_unlocked
#else
#define CGREP_FWRITE fwrite
#endif

/* Prefix and body parts of a typical line fit together here, so the CLI
 * makes one stdio call per line. */
#define EMIT_STAGE 256

/* Zero means an observed output error. */
static int emit(const Search *s, const CuStr *parts, size_t count)
{
    char stage[EMIT_STAGE];
    size_t used;
    size_t i;

    if (s->write != NULL)
        return s->write(s->context, parts, count);
    used = 0;
    for (i = 0; i < count; i++) {
        if (parts[i].len == 0)
            continue;
        if (parts[i].len > sizeof stage - used && used != 0) {
            CGREP_FWRITE(stage, 1, used, s->out);
            used = 0;
        }
        if (parts[i].len > sizeof stage) {
            CGREP_FWRITE(parts[i].ptr, 1, parts[i].len, s->out);
        } else {
            memcpy(stage + used, parts[i].ptr, parts[i].len);
            used += parts[i].len;
        }
    }
    if (used != 0)
        CGREP_FWRITE(stage, 1, used, s->out);
    return !ferror(s->out);
}

/* Search a complete-line window, or the final unterminated tail. *lines
 * counts LF bytes before this window. -1 means an observed output error.
 * Without names or numbers, adjacent matching lines leave as one slice. */
static int search_text(const Search *s, const char *name, CuStr text, unsigned long *lines)
{
    size_t pos;
    size_t m;
    size_t start;
    size_t end;
    size_t counted;
    size_t name_len;
    size_t output_len;
    size_t run_start;
    size_t run_end;
    char number[sizeof(unsigned long) * CHAR_BIT + 2];
    CuStr prefix;
    CuStr parts[5];
    size_t part_count;
    unsigned long seen;
    Finder local;
    Finder *finder;
    int matched;
    int numbers;
    int plain;

    matched = 0;
    pos = 0;
    counted = 0;
    run_start = 0;
    run_end = 0;
    seen = *lines;
    numbers = s->numbers;
    plain = !s->names && !numbers;
    prefix = cu_str_n("", 0);
    name_len = s->names ? strlen(name) : 0;
    finder = s->finder;
    if (finder == NULL) {
        finder_init(&local, &s->literal);
        finder = &local;
    }
    while (pos < text.len && (m = find_with(text, pos, &s->literal, finder)) != NO_MATCH) {
        /* pos is always a line's start, so the match's line starts there
         * or after it. */
        start = m;
        while (start > pos && text.ptr[start - 1] != '\n')
            start--;
        end = m;
        while (end < text.len && text.ptr[end] != '\n')
            end++;
        if (numbers) {
            seen += count_lines(text, counted, start);
            counted = start;
        }
        output_len = end - start;
        if (end < text.len)
            output_len++;
        matched = 1;
        part_count = 0;
        if (plain) {
            if (run_end != start) {
                if (run_end != run_start) {
                    parts[0] = cu_str_n(text.ptr + run_start, run_end - run_start);
                    if (!emit(s, parts, 1))
                        return -1;
                }
                run_start = start;
            }
            run_end = start + output_len;
            if (end < text.len) {
                pos = end + 1;
                continue;
            }
            start = run_start;
            output_len = run_end - run_start;
            run_start = run_end;
        } else {
            if (s->names) {
                parts[part_count++] = cu_str_n(name, name_len);
                parts[part_count++] = cu_str_n(":", 1);
            }
            if (numbers) {
                prefix = number_prefix(number + sizeof number, seen + 1);
                parts[part_count++] = prefix;
            }
        }
        parts[part_count++] = cu_str_n(text.ptr + start, output_len);
        if (end == text.len)
            parts[part_count++] = cu_str_n("\n", 1);
        if (!emit(s, parts, part_count))
            return -1;
        if (end == text.len)
            break;
        pos = end + 1;
    }
    if (run_end != run_start) {
        parts[0] = cu_str_n(text.ptr + run_start, run_end - run_start);
        if (!emit(s, parts, 1))
            return -1;
    }
    if (numbers)
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
        CuStr parts[3];
        size_t count;
        int n;

        count = 0;
        if (s->names) {
            parts[count++] = cu_str_n(name, strlen(name));
            parts[count++] = cu_str_n(": ", 2);
        }
        n = sprintf(warning, "binary file matches (found \"\\0\" byte around offset %lu)\n", offset);
        parts[count++] = cu_str_n(warning, (size_t)n);
        return s->write(s->context, parts, count) ? 1 : -1;
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
    if (s->finder != NULL)
        finder_init(s->finder, &s->literal);
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
    Finder finder;
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
    s.finder = &finder;
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
        if (strcmp(argv[i], "--version") == 0 || strcmp(argv[i], "-V") == 0) {
            fprintf(out, "cgrep %s (%s)\n", CGREP_VERSION, CGREP_REVISION);
            return fflush(out) != 0 || ferror(out) ? 2 : 0;
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
