/* Reuse the real finder seam and naive oracle, not the large native corpus.
 * Enumerated inputs have at most two bytes; targeted inputs have at most 300. */
int cgrep_native_find_suite(void);
#define main cgrep_native_find_suite
#include "../tests/test_find.c"
#undef main

static void binary_word(char *buf, size_t len, size_t word)
{
    size_t i;

    for (i = 0; i < len; i++) {
        buf[i] = (word & 1) != 0 ? (char)233 : 'a';
        word >>= 1;
    }
}

static void check_small(CuStr hay, const Literal *literal)
{
    size_t start;
    size_t cut;
    size_t state;
    size_t anchor;
    size_t alternate;
    int found;
    int expected;

    anchor = literal->anchor;
    alternate = literal->alternate;
    for (start = 0; start <= hay.len + 1; start++) {
        assert(find(hay, start, literal) == naive(hay, start, literal->text));
        if (literal->text.len >= 2)
            assert(find_linear(hay, start, literal) == naive(hay, start, literal->text));
    }
    expected = literal->text.len == 0 ? hay.len != 0 : naive(hay, 0, literal->text) != NO_MATCH;
    for (cut = 0; cut <= hay.len; cut++) {
        state = 0;
        found = contains_chunk(literal, cu_str_n(hay.ptr, cut), &state);
        if (!found)
            found = contains_chunk(literal, cu_str_n(hay.ptr + cut, hay.len - cut), &state);
        assert(found == expected);
    }
    assert(literal->anchor == anchor && literal->alternate == alternate);
}

static void bounded_domain(void)
{
    char hay[2];
    char needle[2];
    CuArena arena;
    Literal literal;
    size_t hlen;
    size_t nlen;
    size_t h;
    size_t n;

    for (nlen = 0; nlen <= 2; nlen++) {
        for (n = 0; n < ((size_t)1 << nlen); n++) {
            binary_word(needle, nlen, n);
            cu_arena_init(&arena);
            assert(literal_init(&literal, &arena, cu_str_n(needle, nlen)));
            for (hlen = 0; hlen <= 2; hlen++) {
                for (h = 0; h < ((size_t)1 << hlen); h++) {
                    binary_word(hay, hlen, h);
                    check_small(cu_str_n(hay, hlen), &literal);
                }
            }
            cu_arena_release(&arena);
        }
    }
}

static void targeted_paths(void)
{
    static const char bytes[] = {'a', '\0', (char)255, (char)233};
    static const char needle[] = {'\0', (char)255};
    static const char hot[] =
        "abababababababababababababababababababababababababababababababababba";
    CuArena arena;
    Literal literal;
    CuStr hay;
    size_t expected;

    cu_arena_init(&arena);
    assert(literal_init(&literal, &arena, cu_str_n(needle, sizeof needle)));
    check_small(cu_str_n(bytes, sizeof bytes), &literal);
    cu_arena_release(&arena);

    cu_arena_init(&arena);
    assert(literal_init(&literal, &arena, CU_LIT("abba")));
    hay = cu_str_n(hot, sizeof hot - 1);
    expected = naive(hay, 0, literal.text);
    assert(expected == sizeof hot - 5);
    confirmations = 0;
    kmp_bytes = 0;
    assert(find(hay, 0, &literal) == expected);
    assert(confirmations == 16 && kmp_bytes != 0);
    assert(find(hay, expected, &literal) == expected);
    assert(find(hay, expected + 1, &literal) == NO_MATCH);
    cu_arena_release(&arena);

    cu_arena_init(&arena);
    assert(literal_init(&literal, &arena, CU_LIT("ababa")));
    check_small(CU_LIT("bababababa"), &literal);
    cu_arena_release(&arena);

    cu_arena_init(&arena);
    fail_pattern_alloc = 1;
    assert(!literal_init(&literal, &arena, CU_LIT("ab")));
    assert(literal.prefix == NULL);
    fail_pattern_alloc = 0;
    cu_arena_release(&arena);
}

static void word_count_corner(void)
{
    static const unsigned char values[] = {'\n', '\v', '\v', '\n', 255, 128, 0, '\n'};
    char bytes[300];
    size_t i;
    unsigned long expected;
    CuArena arena;
    Literal literal;

    expected = 0;
    for (i = 0; i < sizeof bytes; i++) {
        bytes[i] = i < 4 ? '\n' : (char)values[(i - 4) % sizeof values];
        if (bytes[i] == '\n')
            expected++;
    }
    assert(count_lines(cu_str_n(bytes, sizeof bytes), 0, sizeof bytes) == expected);
    memset(bytes, '\n', sizeof bytes);
    assert(count_lines(cu_str_n(bytes, sizeof bytes), 0, sizeof bytes) == (unsigned long)sizeof bytes);
    memset(bytes, 'a', sizeof bytes);
    for (i = 31; i < sizeof bytes; i += 32)
        bytes[i] = '\n';
    cu_arena_init(&arena);
    assert(literal_init(&literal, &arena, CU_LIT("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")));
    kmp_bytes = 0;
    confirmations = 0;
    assert(find(cu_str_n(bytes, sizeof bytes), 0, &literal) == NO_MATCH);
    assert(kmp_bytes == 0 && confirmations == 0);
    cu_arena_release(&arena);
    cu_arena_init(&arena);
    assert(literal_init(&literal, &arena, CU_LIT("aa")));
    assert(find(CU_LIT("abaa"), 0, &literal) == 2);
    cu_arena_release(&arena);
}

int main(void)
{
    bounded_domain();
    test_exact_boundary();
    targeted_paths();
    word_count_corner();
    printf("sem_find: PASS\n");
    return 0;
}
