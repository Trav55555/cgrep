#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
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

static const char usage[] = "usage: cgrep [-0cilnvw] [-j N] [--hidden] [--no-ignore] [--max-depth N] PATTERN [PATH...]\n"
                            "       cgrep --help | --version\n";

static const char help[] =
    "  -h, --help     Print this help.\n"
    "  -V, --version  Print version and source revisions.\n"
    "  -0, --null     Follow printed paths with NUL.\n"
    "  -n             Print line numbers.\n"
    "  -i             Ignore ASCII case.\n"
    "  -w             Match whole words.\n"
    "  -v             Select non-matching lines.\n"
    "  -c             Print selected-line counts.\n"
    "  -l             Print matching file paths.\n"
    "  -j N           Use 1 through 32 workers (default 1).\n";
static const char help_walk[] =
    "  --hidden       Search hidden names, respecting ignore rules.\n"
    "  --no-ignore    Disable ignore files.\n"
    "  --max-depth N  Limit traversal depth (root is depth 0).\n"
    "  --             End option parsing.\n";

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
    int null;    /* -0: terminate printed paths with NUL */
    int numbers; /* -n: prefix each line with its number */
    int names;   /* prefix each line with its input's name */
    int invert;  /* -v: select lines without a match */
    int count;   /* -c: print each input's selected-line count instead */
    int files;   /* -l: print the name of each input with a selected line */
    int fold;    /* -i: ASCII letters match either case; literal is lowercase */
    int word;    /* -w: a match needs a non-word byte or line end on each side */
    int walked;  /* found under a directory argument: a NUL ends the search
                  * without a count, and only warns after printed lines */
    FILE *out;
    /* Borrowed fragments are consumed synchronously; zero stops scanning. */
    int (*write)(void *context, const CuStr *parts, size_t count);
    int (*cancelled)(void *context);
    void *context;
    Finder *finder; /* per-input filter state; NULL uses a fresh one per window */
    CuVec *scratch; /* -i: folded copy of a printed window, owned by the caller */
} Search;

/* Options off, output to out; the caller sets the literal. */
static void search_init(Search *s, FILE *out)
{
    s->null = 0;
    s->numbers = 0;
    s->names = 0;
    s->invert = 0;
    s->count = 0;
    s->files = 0;
    s->fold = 0;
    s->word = 0;
    s->walked = 0;
    s->out = out;
    s->write = NULL;
    s->cancelled = NULL;
    s->context = NULL;
    s->finder = NULL;
    s->scratch = NULL;
}

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

/* ASCII lowercase; other bytes are unchanged, so offsets and LFs stay put.
 * dst may be src. Non-ASCII letters are not folded (the reference folds
 * Unicode). */
static void fold_ascii(char *dst, const char *src, size_t n)
{
    size_t i;
#ifdef CGREP_SSE2
    Bytes16 below;
    Bytes16 above;
    Bytes16 bit;
    Bytes16 v;

    below = splat16('A' - 1);
    above = splat16('Z' + 1);
    bit = splat16(32);
    for (i = 0; n - i >= 16; i += 16) {
        v = load16(src + i);
        /* Bytes from 0x80 are negative here, so never in range. */
        v += (Bytes16)((v > below) & (v < above)) & bit;
        memcpy(dst + i, &v, sizeof v);
    }
#else
    i = 0;
#endif
    for (; i < n; i++)
        dst[i] = (char)(src[i] >= 'A' && src[i] <= 'Z' ? src[i] + 32 : src[i]);
}

/* -w word bytes: ASCII letters, digits and '_', and every byte from 0x80,
 * which counts all non-ASCII characters as word characters (the reference
 * classifies them with Unicode tables). LF is not a word byte. */
static int word_byte(char c)
{
    unsigned char u;

    u = (unsigned char)c;
    return u >= 0x80 || u == '_' || (u >= '0' && u <= '9') || ((u | 32) >= 'a' && (u | 32) <= 'z');
}

/* hay[m, m + len) has no word byte just before or after it. Offset 0 and
 * hay.len count as line boundaries. */
static int word_bounded(CuStr hay, size_t m, size_t len)
{
    return (m == 0 || !word_byte(hay.ptr[m - 1])) &&
           (m + len == hay.len || !word_byte(hay.ptr[m + len]));
}

/* A -w match ending at an open window's end, whose next byte is unread. */
#define UNDECIDED ((size_t)-2)

/* First match at or after pos, with -w only one bounded by non-word bytes.
 * With open_end, hay.len is not a line end, and a match ending there is
 * UNDECIDED (*undecided gets its start). Once a line's first candidate
 * fails its bounds, KMP finds the rest of that line's occurrences, so
 * total work stays linear. */
static size_t find_selected(const Search *s, CuStr hay, size_t pos, Finder *f, int open_end,
                            size_t *undecided)
{
    const Literal *literal;
    const char *nl;
    size_t len;
    size_t m;
    size_t stop;
    size_t state;
    size_t end;
    size_t at;

    literal = &s->literal;
    if (!s->word)
        return find_with(hay, pos, literal, f);
    len = literal->text.len;
    while (pos <= hay.len) {
        m = len == 0 ? pos : find_with(hay, pos, literal, f);
        if (m == NO_MATCH)
            return NO_MATCH;
        nl = (const char *)memchr(hay.ptr + m, '\n', hay.len - m);
        stop = nl == NULL ? hay.len : (size_t)(nl - hay.ptr);
        /* m and every later occurrence that ends in m's line; the KMP scan
         * resumes after each one with its state. */
        state = 0;
        at = m + 1;
        for (;;) {
            if (open_end && m + len == hay.len) {
                *undecided = m;
                return UNDECIDED;
            }
            if (word_bounded(hay, m, len))
                return m;
            if (len == 0) {
                if (m == stop)
                    break;
                m++;
                continue;
            }
            if (len == 1) {
                nl = (const char *)memchr(hay.ptr + m + 1, (unsigned char)literal->text.ptr[0], stop - m - 1);
                if (nl == NULL)
                    break;
                m = (size_t)(nl - hay.ptr);
                continue;
            }
            end = find_end(cu_str_n(hay.ptr, stop), at, literal, &state);
            if (end == NO_MATCH)
                break;
            m = end - len;
            at = end;
        }
        if (stop == hay.len)
            return NO_MATCH;
        pos = stop + 1;
        /* After a final LF no line starts unless more may follow. */
        if (pos == hay.len && !open_end)
            return NO_MATCH;
    }
    return NO_MATCH;
}

/* How selected lines are used: printed, counted, or only detected.
 * Counting and detection stream (scan_window); printing keeps lines. */
typedef enum { SELECT_PRINT, SELECT_COUNT, SELECT_FIRST } SelectMode;

/* Output position within one window. Without names or numbers, adjacent
 * selected lines are held as one run and leave as one slice. */
typedef struct {
    const char *name;
    size_t name_len;
    size_t run_start;
    size_t run_end;
    size_t counted;     /* LFs before this offset are in seen */
    unsigned long seen;
} Printer;

static int flush_run(const Search *s, CuStr text, Printer *p)
{
    CuStr run;

    if (p->run_end == p->run_start)
        return 1;
    run = cu_str_n(text.ptr + p->run_start, p->run_end - p->run_start);
    p->run_start = p->run_end;
    return emit(s, &run, 1);
}

/* Prints the line [start, stop): stop is its LF or text.len. */
static int print_line(const Search *s, CuStr text, size_t start, size_t stop, Printer *p)
{
    char number[sizeof(unsigned long) * CHAR_BIT + 2];
    CuStr parts[5];
    size_t part_count;
    size_t output_len;
    const char *name;
    size_t name_len;

    output_len = stop - start;
    if (stop < text.len)
        output_len++;
    if (!s->names && !s->numbers) {
        if (p->run_end != start) {
            if (!flush_run(s, text, p))
                return 0;
            p->run_start = start;
        }
        p->run_end = start + output_len;
        if (stop < text.len)
            return 1;
        /* An unterminated last line leaves with its added newline. */
        parts[0] = cu_str_n(text.ptr + p->run_start, p->run_end - p->run_start);
        parts[1] = cu_str_n("\n", 1);
        p->run_start = p->run_end;
        return emit(s, parts, 2);
    }
    name = p->name;
    name_len = p->name_len;
    part_count = 0;
    if (s->names) {
        parts[part_count++] = cu_str_n(name, name_len);
        parts[part_count++] = cu_str_n(s->null ? "\0" : ":", 1);
    }
    if (s->numbers) {
        p->seen += count_lines(text, p->counted, start);
        p->counted = start;
        parts[part_count++] = number_prefix(number + sizeof number, p->seen + 1);
    }
    parts[part_count++] = cu_str_n(text.ptr + start, output_len);
    if (stop == text.len)
        parts[part_count++] = cu_str_n("\n", 1);
    if (!emit(s, parts, part_count))
        return 0;
    return 1;
}

/* Selects the lines of text, a complete-line window or the final
 * unterminated tail, that match (or with -v do not). hay has text's bytes
 * as searched (folded for -i). *lines counts LF bytes before this window.
 * Returns 1 if a line was selected, 0 if none, -1 on an output error.
 * SELECT_FIRST stops at the first selected line without output;
 * SELECT_PRINT prints each one. */
static int select_text(const Search *s, const char *name, CuStr text, CuStr hay, unsigned long *lines,
                       SelectMode mode)
{
    Printer p;
    Finder local;
    Finder *finder;
    size_t pos;
    size_t m;
    size_t start;
    size_t stop;
    size_t at;
    size_t undecided;
    const char *nl;
    int matched;

    matched = 0;
    p.name = name;
    p.name_len = s->names ? strlen(name) : 0;
    p.run_start = 0;
    p.run_end = 0;
    p.counted = 0;
    p.seen = *lines;
    finder = s->finder;
    if (finder == NULL) {
        finder_init(&local, &s->literal);
        finder = &local;
    }
    pos = 0;
    while (pos < text.len) {
        m = find_selected(s, hay, pos, finder, 0, &undecided);
        /* pos is always a line's start, so the match's line starts there
         * or after it. */
        start = text.len;
        stop = text.len;
        if (m != NO_MATCH) {
            start = m;
            while (start > pos && text.ptr[start - 1] != '\n')
                start--;
            stop = m;
            while (stop < text.len && text.ptr[stop] != '\n')
                stop++;
        }
        if (s->invert ? start > pos : m != NO_MATCH) {
            matched = 1;
            if (mode != SELECT_PRINT)
                return 1;
            if (!s->invert) {
                if (!print_line(s, text, start, stop, &p))
                    return -1;
            } else {
                /* Every line from pos up to the matching line's start. */
                for (at = pos; at < start; at = (size_t)(nl - text.ptr) + 1) {
                    nl = (const char *)memchr(text.ptr + at, '\n', start - at);
                    if (nl == NULL)
                        nl = text.ptr + start;
                    if (!print_line(s, text, at, (size_t)(nl - text.ptr), &p))
                        return -1;
                }
            }
        }
        if (stop == text.len)
            break;
        pos = stop + 1;
    }
    if (!flush_run(s, text, &p))
        return -1;
    if (s->numbers && mode == SELECT_PRINT)
        *lines = p.seen + count_lines(text, p.counted, text.len);
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
    unsigned long bytes; /* delivered input bytes, for worker batch sizing */
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
    const char *what;

    what = s->walked ? "WARNING: stopped searching binary file after match" : "binary file matches";
    if (s->write != NULL) {
        char warning[sizeof(unsigned long) * CHAR_BIT + 96];
        CuStr parts[3];
        size_t count;
        int n;

        count = 0;
        if (s->names) {
            parts[count++] = cu_str_n(name, strlen(name));
            parts[count++] = cu_str_n(": ", 2);
        }
        n = sprintf(warning, "%s (found \"\\0\" byte around offset %lu)\n", what, offset);
        parts[count++] = cu_str_n(warning, (size_t)n);
        return s->write(s->context, parts, count) ? 1 : -1;
    }
    if (s->names)
        fprintf(s->out, "%s: ", name);
    fprintf(s->out, "%s (found \"\\0\" byte around offset %lu)\n", what, offset);
    return ferror(s->out) ? -1 : 1;
}

/* -c or -l output once an input is done. Returns 0 on an output error. */
static int summary(const Search *s, const char *name, unsigned long selected)
{
    char digits[sizeof(unsigned long) * CHAR_BIT + 2];
    CuStr parts[4];
    size_t k;

    k = 0;
    if (s->count) {
        if (s->names) {
            parts[k++] = cu_str_n(name, strlen(name));
            parts[k++] = cu_str_n(s->null ? "\0" : ":", 1);
        }
        parts[k] = number_prefix(digits + sizeof digits, selected);
        parts[k].len--; /* number_prefix ends with ':' */
        k++;
    } else {
        parts[k++] = cu_str_n(name, strlen(name));
    }
    parts[k++] = cu_str_n(s->null && !s->count ? "\0" : "\n", 1);
    return emit(s, parts, k);
}

/* NUL bytes end lines for -c, -l and a binary input's remaining scan, as
 * in the reference's binary conversion. Patterns contain neither byte, so
 * this creates and removes no match. */
static void nul_to_lf(char *p, size_t n)
{
    char *nul;

    while ((nul = (char *)memchr(p, '\0', n)) != NULL) {
        *nul = '\n';
        n -= (size_t)(nul - p) + 1;
        p = nul + 1;
    }
}

/* Line state carried between windows of a streaming scan. */
typedef struct {
    int marked;             /* the current line has a match */
    int open;               /* the current line has bytes */
    int mid;                /* -w: the window's first byte was carried
                             * mid-line, as context already scanned */
    unsigned long selected;
} LineScan;

/* Scans window w, which starts inside the current line, without keeping
 * whole lines. Returns 1 at the first selected line in SELECT_FIRST mode,
 * else 0. *carry is set to how many trailing bytes the next window must
 * start with: a match may still end in the next chunk. */
static int scan_window(const Search *s, CuStr w, int at_end, SelectMode mode, Finder *finder,
                       LineScan *ls, size_t *carry)
{
    const char *nl;
    size_t pos;
    size_t from;
    size_t stop;
    size_t keep;
    size_t m;
    size_t undecided;

    pos = 0;
    for (;;) {
        nl = (const char *)memchr(w.ptr + pos, '\n', w.len - pos);
        stop = nl == NULL ? w.len : (size_t)(nl - w.ptr);
        /* -w needs each candidate's previous byte: a carried first byte
         * is only context. */
        from = pos == 0 && ls->mid && s->word ? 1 : pos;
        m = NO_MATCH;
        if (!ls->marked && from <= stop)
            m = find_selected(s, cu_str_n(w.ptr, stop), from, finder, nl == NULL && !at_end, &undecided);
        if (m == UNDECIDED) {
            /* Rescan once the next byte is read: from the byte before the
             * candidate as context, or from its line's start. Carrying the
             * previous line's LF would end a line twice. */
            if (undecided > pos) {
                *carry = w.len - (undecided - 1);
                ls->mid = 1;
            } else {
                *carry = w.len - pos;
                ls->mid = pos == 0 && ls->mid;
            }
            ls->open |= pos < w.len;
            return 0;
        }
        if (m != NO_MATCH) {
            ls->marked = 1;
            /* A match selects its line unless inverted: stop reading, once
             * the line is known to exist. A walked file's line counts only
             * once its end has passed the NUL check, as the reference
             * checks each refill before searching it. */
            if (!s->invert && mode == SELECT_FIRST &&
                (s->walked ? nl != NULL : nl != NULL || pos < w.len || ls->open)) {
                ls->selected++;
                return 1;
            }
        }
        if (nl == NULL) {
            ls->open |= pos < w.len;
            break;
        }
        if (ls->marked != s->invert) {
            ls->selected++;
            if (mode == SELECT_FIRST)
                return 1;
        }
        ls->marked = 0;
        ls->open = 0;
        ls->mid = 0;
        pos = stop + 1;
    }
    *carry = 0;
    if (at_end) {
        if (ls->open && ls->marked != s->invert) {
            ls->selected++;
            return mode == SELECT_FIRST;
        }
        return 0;
    }
    /* A match still to complete has fewer than len bytes here; -w also
     * keeps the byte before it. */
    keep = s->literal.text.len;
    if (!s->word && keep != 0)
        keep--;
    if (!ls->marked) {
        if (w.len - pos <= keep) {
            *carry = w.len - pos;
            ls->mid = pos == 0 && ls->mid;
        } else {
            *carry = keep;
            ls->mid = 1;
        }
    }
    return 0;
}

/* The rest of an input after buffer's first `held` bytes, in fixed-size
 * windows. *total counts delivered bytes. Sets result->matched and, for a
 * count, *selected. */
static void scan_stream(const Search *s, CuInput *in, CuVec *buffer, size_t held, SelectMode mode,
                        unsigned long *total, unsigned long *selected, ScanResult *result)
{
    LineScan ls;
    Finder local;
    Finder *finder;
    CuInputStatus status;
    size_t carry;
    size_t want;
    size_t got;
    int at_end;

    ls.marked = 0;
    ls.open = 0;
    ls.mid = 0;
    ls.selected = 0;
    finder = s->finder;
    if (finder == NULL) {
        finder_init(&local, &s->literal);
        finder = &local;
    }
    at_end = 0;
    /* Bytes already held are scanned before anything more is read. */
    if (held != 0) {
        if (scan_window(s, cu_str_n(buffer->data, held), 0, mode, finder, &ls, &carry)) {
            result->matched = 1;
            return;
        }
        memmove(buffer->data, buffer->data + held - carry, carry);
        held = carry;
    }
    for (;;) {
        if (s->cancelled != NULL && s->cancelled(s->context)) {
            result->error = SCAN_CANCELLED;
            return;
        }
        /* held bytes count as the vector's contents so growth keeps them.
         * The carry is shorter than the pattern, so a fixed buffer
         * normally has room. */
        buffer->len = held;
        if (buffer->cap - held < STREAM_CHUNK / 2 && !cu_vec_reserve(buffer, STREAM_CHUNK)) {
            result->error = SCAN_MEMORY;
            return;
        }
        want = buffer->cap - held;
        if (want > STREAM_CHUNK)
            want = STREAM_CHUNK;
        if (*total == 0)
            want = 3;
        status = cu_input_read(in, buffer->data + held, want, &got);
        if (status == CU_INPUT_ERROR) {
            result->error = SCAN_IO;
            result->system_error = cu_input_errno(in);
            return;
        }
        if (status == CU_INPUT_END) {
            got = 0;
            at_end = 1;
        } else if (!add_bytes(total, got)) {
            result->error = SCAN_RANGE;
            return;
        }
        /* A walked binary file is dropped, count and all, unless a listed
         * match came first. */
        if (s->walked && memchr(buffer->data + held, '\0', got) != NULL)
            return;
        nul_to_lf(buffer->data + held, got);
        if (s->fold)
            fold_ascii(buffer->data + held, buffer->data + held, got);
        held += got;
        if (scan_window(s, cu_str_n(buffer->data, held), at_end, mode, finder, &ls, &carry)) {
            result->matched = 1;
            return;
        }
        if (at_end)
            break;
        memmove(buffer->data, buffer->data + held - carry, carry);
        held = carry;
    }
    buffer->len = 0;
    *selected += ls.selected;
    if (ls.selected != 0)
        result->matched = 1;
}

/* The printing selection of one window, searching a folded copy for -i.
 * -2 means the copy could not be allocated. */
static int print_window(const Search *s, const char *name, CuStr text, unsigned long *lines)
{
    CuStr hay;

    hay = text;
    if (s->fold) {
        cu_vec_clear(s->scratch);
        if (!cu_vec_reserve(s->scratch, text.len))
            return -2;
        fold_ascii(s->scratch->data, text.ptr, text.len);
        hay = cu_str_n(s->scratch->data, text.len);
    }
    return select_text(s, name, text, hay, lines, SELECT_PRINT);
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
    unsigned long total;
    unsigned long read_base;
    unsigned long lines;
    unsigned long selected;
    unsigned long nul_offset;
    int probing;
    int found;

    result.matched = 0;
    result.error = SCAN_OK;
    result.system_error = 0;
    result.bytes = 0;
    total = 0;
    lines = 0;
    selected = 0;
    nul_offset = 0;
    /* A failed growth must not poison later inputs. Successful storage is
     * reused across files; only failed vectors discard their arena. */
    if (cu_vec_failed(buffer)) {
        arena = buffer->arena;
        cu_arena_release(arena);
        CU_VEC_INIT(buffer, arena, char);
    }
    if (s->scratch != NULL && cu_vec_failed(s->scratch)) {
        arena = s->scratch->arena;
        cu_arena_release(arena);
        CU_VEC_INIT(s->scratch, arena, char);
    }
    if (s->finder != NULL)
        finder_init(s->finder, &s->literal);
    cu_vec_clear(buffer);
    if (!cu_vec_reserve(buffer, STREAM_CHUNK)) {
        result.error = SCAN_MEMORY;
        return result;
    }
    if (s->count || s->files) {
        scan_stream(s, in, buffer, 0, s->count ? SELECT_COUNT : SELECT_FIRST, &total, &selected, &result);
        if (result.error == SCAN_OK && result.matched && !summary(s, name, selected))
            result.error = SCAN_OUTPUT;
        result.bytes = total;
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
            if (old_len != 0) {
                found = print_window(s, name, cu_str_n(buffer->data, old_len), &lines);
                if (found < 0)
                    result.error = found == -2 ? SCAN_MEMORY : SCAN_OUTPUT;
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
        nul = (const char *)memchr(destination, '\0', got);
        if (nul != NULL) {
            nul_offset = read_base + (unsigned long)(nul - destination);
            /* A walked file's search ends at this refill. */
            if (s->walked) {
                if (result.matched && binary_warning(s, name, nul_offset) < 0)
                    result.error = SCAN_OUTPUT;
                break;
            }
            /* Earlier output stands; one more selected line only warns. */
            if (!result.matched) {
                if (probing) {
                    /* The full buffer is one line, ended by this NUL.
                     * Nothing more is printed, so -i folds in place. */
                    if (s->fold)
                        fold_ascii(buffer->data, buffer->data, old_len);
                    found = select_text(s, name, cu_str_n(buffer->data, old_len), cu_str_n(buffer->data, old_len),
                                        &lines, SELECT_FIRST);
                    if (found > 0)
                        result.matched = 1;
                    else
                        scan_stream(s, in, buffer, 0, SELECT_FIRST, &total, &selected, &result);
                } else {
                    nul_to_lf(destination, got);
                    if (s->fold)
                        fold_ascii(buffer->data, buffer->data, old_len + got);
                    scan_stream(s, in, buffer, old_len + got, SELECT_FIRST, &total, &selected, &result);
                }
            }
            if (result.error == SCAN_OK && result.matched && binary_warning(s, name, nul_offset) < 0)
                result.error = SCAN_OUTPUT;
            break;
        }
        if (probing) {
            if (extra != '\n') {
                result.error = SCAN_LINE_LIMIT;
                break;
            }
            found = print_window(s, name, cu_str_n(buffer->data, old_len), &lines);
            if (found < 0) {
                result.error = found == -2 ? SCAN_MEMORY : SCAN_OUTPUT;
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
        found = print_window(s, name, cu_str_n(buffer->data, end), &lines);
        if (found < 0) {
            result.error = found == -2 ? SCAN_MEMORY : SCAN_OUTPUT;
            break;
        }
        result.matched |= found;
        buffer->len -= end;
        if (buffer->len != 0)
            memmove(buffer->data, buffer->data + end, buffer->len);
    }
    result.bytes = total;
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
static int search_input(const Search *search, const char *path, int walked, FILE *in, FILE *err,
                        CuVec *buffer)
{
    Search copy;
    const Search *s;
    CuInput input;
    ScanResult result;
    const char *name;
    int closed;

    copy = *search;
    copy.walked = walked;
    s = &copy;
    name = path == NULL || strcmp(path, "-") == 0 ? "<stdin>" : path;
    if (name == path) {
        if (!cu_input_open(&input, path)) {
            result.matched = 0;
            result.error = SCAN_IO;
            result.system_error = cu_input_errno(&input);
            result.bytes = 0;
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

static int worker_count(const char *text)
{
    int n;

    n = 0;
    if (*text == '\0')
        return 0;
    while (*text != '\0') {
        if (*text < '0' || *text > '9' || n > 32)
            return 0;
        n = n * 10 + (*text++ - '0');
    }
    return n >= 1 && n <= 32 ? n : 0;
}

/* Parse before clamping so overflowing values remain errors. */
static int parse_depth(const char *text, unsigned long *depth)
{
    unsigned long n;
    unsigned long digit;

    n = 0;
    if (*text == '+')
        text++;
    if (*text == '\0')
        return 0;
    while (*text != '\0') {
        if (*text < '0' || *text > '9')
            return 0;
        digit = (unsigned long)(*text++ - '0');
        if (n > (ULONG_MAX - digit) / 10)
            return 0;
        n = n * 10 + digit;
    }
    *depth = n;
    return 1;
}

typedef struct {
    int hidden;
    int limited;
    unsigned long max_depth;
} WalkOptions;

/* Inputs to search, in order: argument paths, and the files found under
 * directory arguments, which get the reference's handling of binary files
 * found while walking (see Search.walked). With a worker pool the vectors
 * are shared with the pool's threads, touched only under its mutex, and
 * the lister adds to them in runs of INPUT_PENDING. */
#define INPUT_PENDING 64
typedef struct ParallelPool ParallelPool;
typedef struct {
    CuVec paths;  /* char *, borrowed from argv or in the walk arena */
    CuVec walked; /* unsigned char, 1 for a path found by walking */
    ParallelPool *pool; /* NULL: the caller searches after listing */
    int stop;           /* the pool was cancelled: list no more */
    int pending;
    char *pending_paths[INPUT_PENDING];
    unsigned char pending_walked[INPUT_PENDING];
} Inputs;

/* Appends the pending inputs to the pool's list. Zero means out of memory. */
static int inputs_publish(Inputs *inputs);
/* Whether the pool was cancelled; once it was, inputs->stop stays set. */
static int inputs_cancelled(Inputs *inputs);

static int entry_order(const void *a, const void *b)
{
    const CuDirEntry *x;
    const CuDirEntry *y;
    size_t n;
    int c;

    x = (const CuDirEntry *)a;
    y = (const CuDirEntry *)b;
    n = x->name.len < y->name.len ? x->name.len : y->name.len;
    c = memcmp(x->name.ptr, y->name.ptr, n);
    if (c != 0)
        return c;
    return x->name.len < y->name.len ? -1 : x->name.len > y->name.len;
}

static int add_input(Inputs *inputs, char *path, unsigned char walked)
{
    if (inputs->pool == NULL) {
        CU_VEC_PUSH(&inputs->paths, &path);
        CU_VEC_PUSH(&inputs->walked, &walked);
        return !cu_vec_failed(&inputs->paths) && !cu_vec_failed(&inputs->walked);
    }
    inputs->pending_paths[inputs->pending] = path;
    inputs->pending_walked[inputs->pending] = walked;
    if (++inputs->pending < INPUT_PENDING)
        return 1;
    return inputs_publish(inputs);
}

/* dir, then '/' unless dir already ends with one, then name. */
static char *join_path(CuArena *a, const char *dir, CuStr name)
{
    size_t len;
    size_t total;
    size_t slash;
    char *p;

    len = strlen(dir);
    slash = len == 0 || dir[len - 1] != '/';
    if (!cu_size_add(len + slash, name.len, &total) || !cu_size_add(total, 1, &total))
        return NULL;
    p = cu_arena_push_bytes(a, total);
    if (p == NULL)
        return NULL;
    memcpy(p, dir, len);
    if (slash)
        p[len] = '/';
    memcpy(p + len + slash, name.ptr, name.len);
    p[total - 1] = '\0';
    return p;
}

#include "ignore.inc"

/* Adds the regular files under dir, depth first with each directory's
 * names in byte order, skipping symbolic links (never followed), other
 * non-regular entries, names that start with '.' unless an ignore file
 * un-ignores them, and anything an ignore file ignores (st, or none when
 * NULL). Reports each failure, an entry whose ignore rules exceed the
 * matching work limit among them (it is skipped), and returns 0, after
 * adding whatever else it could. */
static int walk(CuArena *a, const char *dir, Inputs *inputs, FILE *err, IgnoreStack *st,
                const WalkOptions *options, unsigned depth)
{
    CuDir d;
    CuDirEntry e;
    CuDirEntry *list;
    CuVec entries;
    char *path;
    const char *error;
    size_t count;
    size_t i;
    unsigned listed;
    int r;
    int ok;
    int verdict;

    if (options->limited && (depth >= options->max_depth || depth >= 256))
        return 1;
    if (inputs->pool != NULL && (inputs->stop || inputs_cancelled(inputs)))
        return 1;
    if (depth > 256) {
        fprintf(err, "cgrep: %s: directory nesting exceeds 256 levels\n", dir);
        return 0;
    }
    if (!cu_dir_open(&d, a, dir)) {
        fprintf(err, "cgrep: %s: %s\n", dir, cu_dir_errno(&d) == 0 ? "out of memory" : strerror(cu_dir_errno(&d)));
        return 0;
    }
    ok = 1;
    listed = 0;
    CU_VEC_INIT(&entries, a, CuDirEntry);
    while ((r = cu_dir_next(&d, &e)) == 1) {
        listed |= ignore_listing_bit(e.name);
        if (e.kind != CU_DIR_FILE && e.kind != CU_DIR_DIR)
            continue;
        if (!options->hidden && st == NULL && e.name.len != 0 && e.name.ptr[0] == '.')
            continue;
        CU_VEC_PUSH(&entries, &e);
    }
    if (r < 0 || cu_vec_failed(&entries)) {
        fprintf(err, "cgrep: %s: %s\n", dir,
                r < 0 && cu_dir_errno(&d) != 0 ? strerror(cu_dir_errno(&d)) : "out of memory");
        ok = 0;
    }
    cu_dir_close(&d);
    if (st != NULL && !push_ignore_dir(st, dir, listed, 0, NULL)) {
        fprintf(err, "cgrep: %s: out of memory\n", dir);
        return 0;
    }
    count = cu_vec_len(&entries);
    list = CU_VEC_DATA(&entries, CuDirEntry);
    if (count > 1)
        qsort(list, count, sizeof *list, entry_order);
    for (i = 0; i < count; i++) {
        path = join_path(a, dir, list[i].name);
        if (path == NULL) {
            fprintf(err, "cgrep: %s: out of memory\n", dir);
            ok = 0;
            break;
        }
        if (st != NULL) {
            verdict = ignore_verdict(st, path, list[i].kind == CU_DIR_DIR, &error);
            if (error != NULL) {
                fprintf(err, "cgrep: %s: %s\n", path, error);
                ok = 0;
                continue;
            }
            if (!options->hidden && list[i].name.len != 0 && list[i].name.ptr[0] == '.' ? verdict != -1 : verdict == 1)
                continue;
        }
        if (list[i].kind == CU_DIR_FILE) {
            if (!add_input(inputs, path, 1)) {
                fprintf(err, "cgrep: %s: out of memory\n", dir);
                ok = 0;
                break;
            }
        } else if (!walk(a, path, inputs, err, st, options, depth + 1)) {
            ok = 0;
        }
    }
    if (st != NULL)
        pop_ignore_dir(st);
    return ok;
}

/* Pushes the directories above root, outermost first, for their ignore
 * files. Without a working directory a relative root has none. */
static int push_ignore_parents(IgnoreStack *st, const char *root)
{
    CuVec abs;
    CuDir d;
    CuArena probe;
    char *absolute_path;
    char *dir;
    char *git;
    const char *c;
    size_t len;
    size_t end;
    size_t i;
    unsigned listed;
    int k;

    st->root = root;
    CU_VEC_INIT(&abs, st->arena, char);
    if (root[0] != '/') {
        if (st->cwd == NULL)
            return 1;
        for (c = st->cwd; *c != '\0'; c++)
            CU_VEC_PUSH(&abs, c);
    }
    /* Components as the reference's paths give them: "." and empty ones
     * dropped, ".." kept. */
    for (c = root; *c != '\0';) {
        while (*c == '/')
            c++;
        len = component_len(c, strlen(c));
        if (len != 0 && !(len == 1 && c[0] == '.')) {
            CU_VEC_PUSH(&abs, "/");
            for (i = 0; i < len; i++)
                CU_VEC_PUSH(&abs, &c[i]);
        }
        c += len;
    }
    CU_VEC_PUSH(&abs, "");
    if (cu_vec_failed(&abs))
        return 0;
    absolute_path = CU_VEC_DATA(&abs, char);
    end = strlen(absolute_path);
    /* Each proper ancestor: "/" first, then longer prefixes. */
    for (len = 0; len < end; len++) {
        if (absolute_path[len] != '/')
            continue;
        dir = cu_arena_push_bytes(st->arena, len == 0 ? 2 : len + 1);
        if (dir == NULL)
            return 0;
        memcpy(dir, absolute_path, len == 0 ? 1 : len);
        dir[len == 0 ? 1 : len] = '\0';
        listed = 0;
        for (k = 0; k < IG_EXCLUDE; k++)
            listed |= 1u << k;
        git = join_path(st->arena, dir, cu_str_n(".git", 4));
        if (git == NULL)
            return 0;
        cu_arena_init(&probe);
        if (cu_dir_open(&d, &probe, git)) {
            cu_dir_close(&d);
            listed |= 1u << IG_KINDS;
        } else if (cu_dir_errno(&d) == ENOTDIR) {
            listed |= 1u << IG_KINDS;
        }
        cu_arena_release(&probe);
        if (!push_ignore_dir(st, dir, listed, 1, absolute_path + len + 1))
            return 0;
    }
    return 1;
}

/* Whether path names a directory that can be listed, following symbolic
 * links as the reference does for arguments. Anything else is opened as a
 * file, which reports why it cannot be read (an unlistable directory
 * fails to open with the same error). */
static int directory_argument(const char *path)
{
    CuDir d;
    CuArena probe;
    int r;

    cu_arena_init(&probe);
    r = cu_dir_open(&d, &probe, path);
    if (r)
        cu_dir_close(&d);
    cu_arena_release(&probe);
    return r;
}

/* The path arguments and how to expand them into inputs. Everything here
 * is used by whichever thread runs list_inputs. */
typedef struct {
    char **args;
    const unsigned char *directories; /* per argument: list it as a directory */
    int count;
    int ignoring;
    WalkOptions options;
    IgnoreStack ignores;
    CuArena *arena;
    char *cwd;        /* room for the working directory, or NULL */
    size_t cwd_size;
    Inputs inputs;
} Listing;

/* Expands the arguments into inputs, in argument order, reporting walk
 * failures on err. Returns 0 after any failure. */
static int list_inputs(Listing *l, FILE *err)
{
    IgnoreDir unused_dir;
    const char *config;
    int global_read;
    int failed;
    int r;
    int k;

    global_read = 0;
    failed = 0;
    for (k = 0; k < l->count; k++) {
        if (l->inputs.stop)
            break;
        if (!l->directories[k]) {
            if (!add_input(&l->inputs, l->args[k], 0)) {
                fputs("cgrep: out of memory listing inputs\n", err);
                failed = 1;
                break;
            }
            continue;
        }
        if (l->ignoring && !global_read) {
            global_read = 1;
            r = 1;
#ifdef CGREP_GETCWD
            l->ignores.cwd = l->cwd == NULL ? NULL : getcwd(l->cwd, l->cwd_size);
#endif
            config = getenv("XDG_CONFIG_HOME");
            if (config != NULL && config[0] != '\0')
                r = read_ignore(l->arena, config, "git/ignore", &l->ignores.global);
            else if ((config = getenv("HOME")) != NULL && config[0] != '\0')
                r = read_ignore(l->arena, config, ".config/git/ignore", &l->ignores.global);
            if (!r) {
                fputs("cgrep: out of memory reading ignore files\n", err);
                failed = 1;
                break;
            }
        }
        if (l->ignoring && !push_ignore_parents(&l->ignores, l->args[k])) {
            fputs("cgrep: out of memory reading ignore files\n", err);
            failed = 1;
            break;
        }
        if (!walk(l->arena, l->args[k], &l->inputs, err, l->ignoring ? &l->ignores : NULL, &l->options, 0))
            failed = 1;
        /* The next root has its own parents. */
        while (CU_VEC_POP(&l->ignores.dirs, &unused_dir))
            ;
    }
    if (l->inputs.pool != NULL && l->inputs.pending != 0 && !inputs_publish(&l->inputs)) {
        fputs("cgrep: out of memory listing inputs\n", err);
        failed = 1;
    }
    return !failed;
}

#include "parallel.inc"

int cgrep_main(int argc, char **argv, FILE *in, FILE *out, FILE *err)
{
    Search s;
    Listing listing;
    Finder finder;
    CuArena pattern_arena;
    CuArena input_arena;
    CuArena scratch_arena;
    CuVec buffer;
    CuVec scratch;
    CuArena walk_arena;
    char cwd[4096];
    unsigned char *directories;
    CuStr pattern;
    char *folded;
    char **paths;
    unsigned char *walked;
    int count;
    int k;
    int i;
    int r;
    int matched;
    int failed;
    int jobs;
    int stdin_named;
    int directory_named;
    const char *flag;

    search_init(&s, out);
    s.finder = &finder;
    listing.options.hidden = 0;
    listing.options.limited = 0;
    listing.options.max_depth = 0;
    listing.ignoring = 1;
    jobs = 0;
    i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            fputs(usage, out);
            fputs(help, out);
            fputs(help_walk, out);
            return fflush(out) != 0 || ferror(out) ? 2 : 0;
        }
        if (strcmp(argv[i], "--hidden") == 0) {
            listing.options.hidden = 1;
            i++;
            continue;
        }
        if (strcmp(argv[i], "--null") == 0) {
            s.null = 1;
            i++;
            continue;
        }
        if (strcmp(argv[i], "--max-depth") == 0 ||
            strncmp(argv[i], "--max-depth=", 12) == 0) {
            flag = argv[i];
            if (argv[i][11] == '\0')
                flag = i + 1 < argc ? argv[++i] : "";
            else
                flag += 12;
            if (!parse_depth(flag, &listing.options.max_depth)) {
                fputs("cgrep: max depth must be a non-negative integer in range\n", err);
                return 2;
            }
            listing.options.limited = 1;
            i++;
            continue;
        }
        if (strcmp(argv[i], "--no-ignore") == 0) {
            listing.ignoring = 0;
            i++;
            continue;
        }
        if (strcmp(argv[i], "--version") == 0) {
            fprintf(out, "cgrep %s (%s)\n", CGREP_VERSION, CGREP_REVISION);
            return fflush(out) != 0 || ferror(out) ? 2 : 0;
        }
        /* Short flags cluster: -vn, -cj4. A value flag takes the rest of
         * its argument or, if that is empty, the next one. */
        for (flag = argv[i] + 1; *flag != '\0'; flag++) {
            if (*flag == 'h') {
                fputs(usage, out);
                fputs(help, out);
                fputs(help_walk, out);
                return fflush(out) != 0 || ferror(out) ? 2 : 0;
            } else if (*flag == '0') {
                s.null = 1;
            } else if (*flag == 'n') {
                s.numbers = 1;
            } else if (*flag == 'c') {
                s.count = 1;
            } else if (*flag == 'l') {
                /* -l replaces an earlier -c; a later -c wins, as it is
                 * checked first. */
                s.files = 1;
                s.count = 0;
            } else if (*flag == 'v') {
                s.invert = 1;
            } else if (*flag == 'i') {
                s.fold = 1;
            } else if (*flag == 'w') {
                s.word = 1;
            } else if (*flag == 'V') {
                fprintf(out, "cgrep %s (%s)\n", CGREP_VERSION, CGREP_REVISION);
                return fflush(out) != 0 || ferror(out) ? 2 : 0;
            } else if (*flag == 'j') {
                if (flag[1] != '\0')
                    jobs = worker_count(flag + 1);
                else if (i + 1 < argc)
                    jobs = worker_count(argv[++i]);
                else
                    jobs = 0;
                if (jobs == 0) {
                    fputs("cgrep: worker count must be 1 through 32\n", err);
                    return 2;
                }
                break;
            } else {
                fprintf(err, "cgrep: unknown flag %s\n%s", argv[i], usage);
                return 2;
            }
        }
        i++;
    }
    if (i >= argc) {
        fputs(usage, err);
        return 2;
    }
    if (jobs == 0)
        jobs = default_workers();
    pattern = cu_str_cstr(argv[i++]);
    if (memchr(pattern.ptr, '\n', pattern.len) != NULL) {
        fputs("cgrep: a pattern cannot contain a newline\n", err);
        return 2;
    }
    cu_arena_init(&pattern_arena);
    if (s.fold && pattern.len != 0) {
        folded = cu_arena_push_bytes(&pattern_arena, pattern.len);
        if (folded == NULL) {
            fputs("cgrep: out of memory preparing pattern\n", err);
            cu_arena_release(&pattern_arena);
            return 2;
        }
        fold_ascii(folded, pattern.ptr, pattern.len);
        pattern = cu_str_n(folded, pattern.len);
    }
    if (!literal_init(&s.literal, &pattern_arena, pattern)) {
        fputs("cgrep: out of memory preparing pattern\n", err);
        cu_arena_release(&pattern_arena);
        return 2;
    }
    cu_arena_init(&input_arena);
    CU_VEC_INIT(&buffer, &input_arena, char);
    /* Its own arena: growing alternately with buffer would move both. */
    cu_arena_init(&scratch_arena);
    CU_VEC_INIT(&scratch, &scratch_arena, char);
    s.scratch = &scratch;
    s.names = argc - i > 1;
    matched = 0;
    failed = 0;
    /* Directory arguments expand, in place, to the files under them. */
    cu_arena_init(&walk_arena);
    listing.args = argv + i;
    listing.count = argc - i;
    listing.arena = &walk_arena;
    listing.cwd = cwd;
    listing.cwd_size = sizeof cwd;
    CU_VEC_INIT(&listing.inputs.paths, &walk_arena, char *);
    CU_VEC_INIT(&listing.inputs.walked, &walk_arena, unsigned char);
    listing.inputs.pool = NULL;
    listing.inputs.stop = 0;
    listing.inputs.pending = 0;
    listing.ignores.arena = &walk_arena;
    CU_VEC_INIT(&listing.ignores.dirs, &walk_arena, IgnoreDir);
    listing.ignores.global.rules = NULL;
    listing.ignores.global.count = 0;
    listing.ignores.cwd = NULL;
    directories = (unsigned char *)cu_arena_push_bytes(&walk_arena, listing.count == 0 ? 1 : (size_t)listing.count);
    if (directories == NULL) {
        fputs("cgrep: out of memory listing inputs\n", err);
        cu_arena_release(&walk_arena);
        cu_arena_release(&scratch_arena);
        cu_arena_release(&input_arena);
        cu_arena_release(&pattern_arena);
        return 2;
    }
    listing.directories = directories;
    stdin_named = 0;
    directory_named = 0;
    for (k = 0; k < listing.count; k++) {
        r = strcmp(listing.args[k], "-") == 0;
        stdin_named |= r;
        directories[k] = !r && directory_argument(listing.args[k]);
        if (directories[k]) {
            s.names = 1;
            directory_named = 1;
        }
    }
    if (jobs > 1 && !stdin_named && (directory_named || listing.count > 1) &&
        parallel_search(&s, &listing, jobs, in, out, err, &r)) {
        cu_arena_release(&walk_arena);
        cu_arena_release(&scratch_arena);
        cu_arena_release(&input_arena);
        cu_arena_release(&pattern_arena);
        return r;
    }
    if (!list_inputs(&listing, err))
        failed = 1;
    count = (int)cu_vec_len(&listing.inputs.paths);
    paths = CU_VEC_DATA(&listing.inputs.paths, char *);
    walked = CU_VEC_DATA(&listing.inputs.walked, unsigned char);
    if (i == argc) {
        r = search_input(&s, NULL, 0, in, err, &buffer);
        matched = r == 1;
        failed = r < 0;
    }
    for (k = 0; k < count; k++) {
        r = search_input(&s, paths[k], walked[k], in, err, &buffer);
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
    cu_arena_release(&walk_arena);
    cu_arena_release(&scratch_arena);
    cu_arena_release(&input_arena);
    cu_arena_release(&pattern_arena);
    return failed ? 2 : matched ? 0 : 1;
}
