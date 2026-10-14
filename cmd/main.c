#include <stdio.h>
#include "cgrep.h"
#if defined(__linux__) && defined(_GNU_SOURCE) && !defined(__cerb__)
#include <unistd.h>
#endif

int main(int argc, char **argv)
{
#if defined(__linux__) && defined(_GNU_SOURCE) && !defined(__cerb__)
    static char output_buffer[65536];

    /* Keep TTY latency and borrowed library streams' existing setup. */
    if (!isatty(fileno(stdout)) && setvbuf(stdout, output_buffer, _IOFBF, sizeof output_buffer) != 0) {
        fputs("cgrep: output setup error\n", stderr);
        return 2;
    }
#endif
    return cgrep_main(argc, argv, stdin, stdout, stderr);
}
