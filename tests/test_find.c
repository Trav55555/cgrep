/* Finder properties against a naive byte-search oracle, plus a bounded
 * confirmation sensor for inputs that defeat the candidate heuristic. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cu.h"
#include "cgrep.h"

static size_t confirmations;
static size_t newline_scans;
static size_t kmp_bytes;
static int fail_pattern_alloc;

static int counted_memcmp(const void *a, const void *b, size_t n)
{
    confirmations++;
    return memcmp(a, b, n);
}

static void *counted_memchr(const void *p, int c, size_t n)
{
    if (c == '\n')
        newline_scans++;
    return memchr(p, c, n);
}

static void *pattern_push(CuArena *a, size_t count, size_t size)
{
    if (fail_pattern_alloc)
        cu_arena_fail_at(a, 0);
    return cu_arena_push(a, count, size);
}

int cgrep_find_probe(int argc, char **argv, FILE *in, FILE *out, FILE *err);

#define CGREP_KMP_STEP() (kmp_bytes++)
#define memcmp counted_memcmp
#define memchr counted_memchr
#define cu_arena_push pattern_push
#define cgrep_main cgrep_find_probe
#include "../src/cgrep.c"
#undef memcmp
#undef memchr
#undef cu_arena_push
#undef cgrep_main
#undef CGREP_KMP_STEP

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

static size_t words(size_t len)
{
    size_t n;
    size_t i;

    n = 1;
    for (i = 0; i < len; i++)
        n *= 4;
    return n;
}

static void decode(char *buf, size_t len, size_t word)
{
    static const unsigned char alphabet[] = {0, 'a', 233, 255};
    size_t i;

    for (i = 0; i < len; i++) {
        buf[i] = (char)alphabet[word % 4];
        word /= 4;
    }
}

static void test_small_domain(void)
{
    char haybuf[5];
    char needlebuf[4];
    CuArena a;
    Literal literal;
    CuStr hay;
    CuStr needle;
    size_t hlen;
    size_t nlen;
    size_t h;
    size_t n;
    size_t start;
    size_t cut;
    size_t state;
    int found;
    int expected;

    for (nlen = 0; nlen <= sizeof needlebuf; nlen++) {
        for (n = 0; n < words(nlen); n++) {
            decode(needlebuf, nlen, n);
            needle = cu_str_n(needlebuf, nlen);
            cu_arena_init(&a);
            assert(literal_init(&literal, &a, needle));
            for (hlen = 0; hlen <= sizeof haybuf; hlen++) {
                for (h = 0; h < words(hlen); h++) {
                    decode(haybuf, hlen, h);
                    hay = cu_str_n(haybuf, hlen);
                    for (start = 0; start <= hlen + 1; start++) {
                        assert(find(hay, start, &literal) == naive(hay, start, needle));
                        if (needle.len >= 2)
                            assert(find_linear(hay, start, &literal) == naive(hay, start, needle));
                    }
                    expected = needle.len == 0 ? hay.len != 0 : naive(hay, 0, needle) != NO_MATCH;
                    for (cut = 0; cut <= hlen; cut++) {
                        state = 0;
                        found = contains_chunk(&literal, cu_str_n(hay.ptr, cut), &state);
                        if (!found)
                            found = contains_chunk(&literal, cu_str_n(hay.ptr + cut, hlen - cut), &state);
                        assert(found == expected);
                    }
                }
            }
            cu_arena_release(&a);
        }
    }
}

static void test_exact_boundary(void)
{
    char *bytes;
    CuArena a;
    Literal literal;
    CuStr hay;

    bytes = (char *)malloc(3);
    assert(bytes != NULL);
    memcpy(bytes, "aab", 3);
    hay = cu_str_n(bytes, 3);
    cu_arena_init(&a);
    assert(literal_init(&literal, &a, CU_LIT("ab")));
    assert(find(hay, 0, &literal) == 1);
    assert(find(hay, 1, &literal) == 1);
    assert(find(hay, 2, &literal) == NO_MATCH);
    cu_arena_release(&a);
    free(bytes);
}

static void test_selective_candidate(void)
{
    static const char *const patterns[] = {"baaaaaaaa", "aaaabaaaa", "aaaaaaaab", "aba"};
    char bytes[64];
    CuArena a;
    Literal literal;
    size_t i;

    memset(bytes, 'a', sizeof bytes);
    for (i = 0; i < sizeof patterns / sizeof patterns[0]; i++) {
        cu_arena_init(&a);
        assert(literal_init(&literal, &a, cu_str_cstr(patterns[i])));
        confirmations = 0;
        assert(find(cu_str_n(bytes, sizeof bytes), 0, &literal) == NO_MATCH);
        /* An absent selective byte should reject this input without any
         * full-pattern comparisons, wherever that byte sits in the pattern. */
        assert(confirmations == 0);
        cu_arena_release(&a);
    }
}

static void test_misleading_anchor(void)
{
    char bytes[4096];
    char *exact;
    CuArena a;
    Literal literal;

    memset(bytes, 'b', sizeof bytes);
    cu_arena_init(&a);
    assert(literal_init(&literal, &a, CU_LIT("baaaaaaaa")));
    confirmations = 0;
    kmp_bytes = 0;
    assert(find(cu_str_n(bytes, sizeof bytes), 0, &literal) == NO_MATCH);
    /* The alternate byte is absent, so the filter should reject the
     * suffix without making KMP visit every remaining byte. */
    assert(confirmations <= 16 && kmp_bytes == 0);
    exact = (char *)malloc(137);
    assert(exact != NULL);
    memset(exact, 'b', 128);
    memcpy(exact + 128, "baaaaaaaa", 9);
    confirmations = 0;
    assert(find(cu_str_n(exact, 137), 0, &literal) == 128);
    assert(confirmations <= 16);
    free(exact);
    cu_arena_release(&a);
}

static void test_fallback(void)
{
    char overlap[18];
    char dual_overlap[26];
    char *bytes;
    char needlebuf[66];
    CuArena a;
    Literal literal;
    CuStr hay;
    CuStr needle;
    size_t i;
    size_t len;

    /* Seven false candidates, then one whose anchor lies inside the real
     * match. Resuming after that anchor instead of candidate + 1 loses it. */
    memset(overlap, 'c', 12);
    memcpy(overlap + 12, "abcabc", 6);
    cu_arena_init(&a);
    assert(literal_init(&literal, &a, CU_LIT("abcabc")));
    confirmations = 0;
    assert(find(cu_str_n(overlap, sizeof overlap), 0, &literal) == 12);
    assert(confirmations <= 16);
    cu_arena_release(&a);

    /* Eight failed primary candidates and eight failed alternate ones.
     * The final alternate byte lies inside the real overlapping match. */
    memset(dual_overlap, 'c', 13);
    memset(dual_overlap + 13, 'b', 7);
    memcpy(dual_overlap + 20, "abcabc", 6);
    cu_arena_init(&a);
    assert(literal_init(&literal, &a, CU_LIT("abcabc")));
    confirmations = 0;
    kmp_bytes = 0;
    assert(find(cu_str_n(dual_overlap, sizeof dual_overlap), 0, &literal) == 20);
    assert(confirmations <= 16 && kmp_bytes != 0);
    cu_arena_release(&a);

    /* Each false candidate agrees on a long prefix. Enough candidates
     * must cause bounded confirmation work rather than N full comparisons. */
    memset(needlebuf, 'a', 64);
    needlebuf[64] = 'b';
    needlebuf[65] = 'c';
    needle = cu_str_n(needlebuf, sizeof needlebuf);
    len = 65 * sizeof needlebuf;
    bytes = (char *)malloc(len);
    assert(bytes != NULL);
    for (i = 0; i < 64; i++) {
        memset(bytes + i * sizeof needlebuf, 'a', 63);
        memcpy(bytes + i * sizeof needlebuf + 63, "xbc", 3);
    }
    memcpy(bytes + 64 * sizeof needlebuf, needlebuf, sizeof needlebuf);
    hay = cu_str_n(bytes, len);
    cu_arena_init(&a);
    assert(literal_init(&literal, &a, needle));
    confirmations = 0;
    assert(find(hay, 0, &literal) == naive(hay, 0, needle));
    assert(confirmations <= 16);
    bytes[len - 1] = 'x';
    confirmations = 0;
    assert(find(hay, 0, &literal) == NO_MATCH);
    assert(confirmations <= 16);
    cu_arena_release(&a);
    free(bytes);
}

static void test_optional_numbering(void)
{
    CuArena a;
    Search s;
    CuStr text;
    unsigned long lines;

    cu_arena_init(&a);
    assert(literal_init(&s.literal, &a, CU_LIT("needle")));
    s.numbers = 0;
    s.names = 0;
    s.out = stdout;
    text = CU_LIT("before\n\nneedle\n");
    newline_scans = 0;
    lines = 0;
    assert(search_text(&s, "input", text, &lines) == 1);
    assert(newline_scans == 0 && lines == 0);
    s.numbers = 1;
    newline_scans = 0;
    assert(search_text(&s, "input", text, &lines) == 1);
    assert(newline_scans != 0);
    cu_arena_release(&a);
}

static void test_pattern_failure(void)
{
    char *argv[4];
    CuArena a;
    Literal literal;

    fail_pattern_alloc = 1;
    cu_arena_init(&a);
    assert(!literal_init(&literal, &a, CU_LIT("ab")));
    assert(literal.prefix == NULL && cu_arena_failed(&a));
    cu_arena_release(&a);
    argv[0] = (char *)"cgrep";
    argv[1] = (char *)"ab";
    argv[2] = (char *)"not-opened";
    argv[3] = NULL;
    assert(cgrep_find_probe(3, argv, stdin, stdout, stderr) == 2);
    fail_pattern_alloc = 0;
}

int main(void)
{
    test_small_domain();
    test_exact_boundary();
    test_selective_candidate();
    test_misleading_anchor();
    test_fallback();
    test_optional_numbering();
    test_pattern_failure();
    return 0;
}
