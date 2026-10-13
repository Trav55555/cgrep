/* Bounded portable writer/worker-option semantics; no pthread interpretation. */
int portable_parallel_cases(int argc, char **argv);
#define main portable_parallel_cases
#include "../tests/test_parallel.c"
#undef main

int main(void)
{
    int result;

    result = portable_parallel_cases(1, NULL);
    assert(result == 0);
    puts("sem_parallel: PASS (portable writer and options; native workers not interpreted)");
    return result;
}
