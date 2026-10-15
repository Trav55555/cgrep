/* Reuse the real CLI/fault-injection seam. Payloads have at most 24 bytes;
 * production chunk and line-limit constants are not replaced. */
int cgrep_native_stream_suite(void);
#define main cgrep_native_stream_suite
#include "../tests/test_stream.c"
#undef main

static void bounded_binary(void)
{
    static const char *const args[] = {"alpha"};
    static const char *const empty[] = {""};

    reset_faults();
    read_limit = 1;
    assert(run(1, args, "\0alpha", 6) == 0);
    assert(strcmp(output_text, "binary file matches (found \"\\0\" byte around offset 0)\n") == 0);
    assert(error_text[0] == '\0' && read_calls == 6 && close_calls == 1);
    reset_faults();
    assert(run(1, empty, "\n\n\n\0", 4) == 0);
    assert(strcmp(output_text, "\n\n\nbinary file matches (found \"\\0\" byte around offset 3)\n") == 0);
    assert(error_text[0] == '\0');
}

static void scanner_read_error(void)
{
    Search search;
    ScanResult result;
    CuArena pattern_arena;
    CuArena input_arena;
    CuVec buffer;
    CuInput input;
    FILE *in;
    FILE *out;
    char filename[512];

    reset_faults();
    fail_read_at = 2;
    write_file("in", "a\nunfinished", 12);
    file_path(filename, "in");
    in = fopen(filename, "rb");
    file_path(filename, "out");
    out = fopen(filename, "wb");
    assert(in != NULL && out != NULL);
    cu_arena_init(&pattern_arena);
    cu_arena_init(&input_arena);
    CU_VEC_INIT(&buffer, &input_arena, char);
    assert(literal_init(&search.literal, &pattern_arena, CU_LIT("a")));
    search.numbers = 0;
    search.names = 0;
    search.out = out;
    search.write = NULL;
    search.cancelled = NULL;
    search.finder = NULL;
    search.context = NULL;
    cu_input_attach(&input, in);
    result = search_stream(&search, "<stdin>", &input, &buffer);
    assert(result.matched == 1 && result.error == SCAN_IO && result.system_error == EDOM);
    assert(!cu_input_close(&input));
    assert(fclose(in) == 0 && fclose(out) == 0);
    capture(output_text, sizeof output_text, "out");
    assert(strcmp(output_text, "a\n") == 0);
    cu_arena_release(&input_arena);
    cu_arena_release(&pattern_arena);
}

static void bounded_failures(void)
{
    static const char *const files[] = {"alpha", "@first", "@second"};
    static const char *const one[] = {"a"};

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
    assert(strstr(error_text, strerror(ERANGE)) != NULL && close_calls == 2);
    reset_faults();
    fail_read_at = 1;
    fail_close_at = 1;
    assert(run(3, files, "", 0) == 2);
    assert(strstr(output_text, "first:alpha") == NULL);
    assert(strstr(output_text, "second:alpha\n") != NULL);
    assert(strstr(error_text, strerror(EDOM)) != NULL);
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
    bounded_binary();
    scanner_read_error();
    bounded_failures();
    test_offsets();
    printf("sem_stream: PASS\n");
    return 0;
}
