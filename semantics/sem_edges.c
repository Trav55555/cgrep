/* The CLI suite's long-option, boundary, literal-shape and output-error
 * cases, which do not fit sem_cli's Cerberus time limit together with the
 * rest of the suite. */
int cgrep_native_cli_suite(void);
#define main cgrep_native_cli_suite
#include "../tests/test_cgrep.c"
#undef main

int main(void)
{
    const char *tmp;

    tmp = getenv("TMPDIR");
    if (tmp == NULL || tmp[0] == '\0')
        tmp = "/tmp";
    assert(strlen(tmp) < sizeof dir);
    strcpy(dir, tmp);
    test_long_names();
    test_boundaries_and_explicit_stdin();
    test_literal_shapes();
    test_output_error();
    printf("sem_edges: PASS\n");
    return 0;
}
