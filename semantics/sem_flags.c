/* The CLI suite's output-staging and selection-flag cases, which do not fit
 * sem_cli's Cerberus time limit together with the rest of the suite. */
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
    test_staged_output();
    test_selection_flags();
    printf("sem_flags: PASS\n");
    return 0;
}
