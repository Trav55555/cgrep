#include <assert.h>
#include <stdio.h>
#if defined(__linux__) && defined(_GNU_SOURCE) && !defined(__cerb__)
#include <unistd.h>
#endif

static int scans;
#define cgrep_main cli_scan_stub
#include "cgrep.h"

int cli_scan_stub(int argc, char **argv, FILE *in, FILE *out, FILE *err)
{
    (void)argc;
    (void)argv;
    (void)in;
    (void)out;
    (void)err;
    scans++;
    return 0;
}

#if defined(__linux__) && defined(_GNU_SOURCE) && !defined(__cerb__)
static int tty;
static int buffers;
static int refuse;

static int test_isatty(int fd)
{
    (void)fd;
    return tty;
}

static int test_setvbuf(FILE *file, char *bytes, int mode, size_t size)
{
    assert(file == stdout && bytes != NULL && mode == _IOFBF && size == 65536);
    buffers++;
    return refuse;
}
#define isatty test_isatty
#define setvbuf test_setvbuf
#endif
int cli_entry(int argc, char **argv);
#define main cli_entry
#include "../cmd/main.c"
#undef main
#undef cgrep_main

int main(void)
{
#if defined(__linux__) && defined(_GNU_SOURCE) && !defined(__cerb__)
    tty = 1;
    assert(cli_entry(0, NULL) == 0 && scans == 1 && buffers == 0);
    tty = 0;
    assert(cli_entry(0, NULL) == 0 && scans == 2 && buffers == 1);
    refuse = 1;
    assert(cli_entry(0, NULL) == 2 && scans == 2 && buffers == 2);
#else
    assert(cli_entry(0, NULL) == 0 && scans == 1);
#endif
    puts("test_cli_buffer: PASS");
    return 0;
}
