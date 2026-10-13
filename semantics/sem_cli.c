/* The existing CLI suite is small enough to run in full on Cerberus. */
int cgrep_native_cli_suite(void);
#define main cgrep_native_cli_suite
#include "../tests/test_cgrep.c"
#undef main

int main(void)
{
    assert(cgrep_native_cli_suite() == 0);
    printf("sem_cli: PASS\n");
    return 0;
}
