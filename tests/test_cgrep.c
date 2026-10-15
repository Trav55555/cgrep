/* cgrep_main against the ripgrep behaviour it reproduces (non-terminal
 * output). Inputs and outputs are files in TMPDIR (dcheck gives wasm tests
 * one); expected texts come from running ripgrep 14.1.1 on the same input. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cgrep.h"

static char dir[400];

static void path_of(char *path, const char *name)
{
    sprintf(path, "%s/cgrep-test-%s", dir, name);
}

static void write_file(const char *name, const char *data, size_t n)
{
    char path[512];
    FILE *f;

    path_of(path, name);
    f = fopen(path, "wb");
    assert(f != NULL);
    assert(n == 0 || fwrite(data, 1, n, f) == n);
    assert(fclose(f) == 0);
}

/* The whole of a file, NUL-terminated, in a static buffer. */
static const char *slurp(const char *path)
{
    static char buf[8192];
    FILE *f;
    size_t n;

    f = fopen(path, "rb");
    assert(f != NULL);
    n = fread(buf, 1, sizeof buf - 1, f);
    assert(feof(f) && !ferror(f));
    assert(fclose(f) == 0);
    buf[n] = '\0';
    return buf;
}

static char out_text[8192];
static char err_text[8192];

/* Runs cgrep with the arguments (paths named by test name are expanded to
 * their TMPDIR path when they start with '@'), stdin from the named file
 * or empty, and captures stdout and stderr. Returns the exit status. */
static int run(const char *stdin_name, int argc, const char *const *args)
{
    char *argv[16];
    char paths[16][512];
    char in_path[512];
    char out_path[512];
    char err_path[512];
    FILE *in;
    FILE *out;
    FILE *err;
    int i;
    int status;

    assert(argc < 15);
    argv[0] = (char *)"cgrep";
    for (i = 0; i < argc; i++) {
        if (args[i][0] == '@') {
            path_of(paths[i], args[i] + 1);
            argv[i + 1] = paths[i];
        } else {
            argv[i + 1] = (char *)args[i];
        }
    }
    argv[argc + 1] = NULL;
    write_file("stdin-empty", "", 0);
    path_of(in_path, stdin_name != NULL ? stdin_name : "stdin-empty");
    path_of(out_path, "out");
    path_of(err_path, "err");
    in = fopen(in_path, "rb");
    out = fopen(out_path, "wb");
    err = fopen(err_path, "wb");
    assert(in != NULL && out != NULL && err != NULL);
    status = cgrep_main(argc + 1, argv, in, out, err);
    assert(fclose(in) == 0 && fclose(out) == 0 && fclose(err) == 0);
    strcpy(out_text, slurp(out_path));
    strcpy(err_text, slurp(err_path));
    return status;
}

/* out_text with the TMPDIR prefix of every path removed, for comparison
 * with expected texts written with bare names. */
static const char *out_bare(void)
{
    static char buf[8192];
    char prefix[512];
    const char *p;
    size_t n;
    size_t used;

    path_of(prefix, "");
    n = strlen(prefix);
    used = 0;
    for (p = out_text; *p != '\0';) {
        if (strncmp(p, prefix, n) == 0) {
            p += n;
        } else {
            assert(used < sizeof buf - 1);
            buf[used++] = *p++;
        }
    }
    buf[used] = '\0';
    return buf;
}

static void test_one_file(void)
{
    static const char *const args[] = {"alpha", "@t2"};

    write_file("t2", "no\nalpha here\n", 14);
    assert(run(NULL, 2, args) == 0);
    assert(strcmp(out_text, "alpha here\n") == 0);
    assert(err_text[0] == '\0');
}

static void test_several_files_and_numbers(void)
{
    static const char *const plain[] = {"alpha", "@a", "@b"};
    static const char *const numbered[] = {"-n", "alpha", "@a", "@b"};

    write_file("a", "alpha one\nbeta\nalpha alpha two\n", 31);
    write_file("b", "nothing\nlast alpha", 18);
    assert(run(NULL, 3, plain) == 0);
    assert(strcmp(out_bare(), "a:alpha one\na:alpha alpha two\nb:last alpha\n") == 0);
    assert(run(NULL, 4, numbered) == 0);
    assert(strcmp(out_bare(), "a:1:alpha one\na:3:alpha alpha two\nb:2:last alpha\n") == 0);
}

static void test_literal_path_prefixes(void)
{
    static const char *const plain[] = {"alpha", "@%s: space\303\251", "@tail"};
    static const char *const numbered[] = {"-n", "alpha", "@%s: space\303\251", "@tail"};

    write_file("%s: space\303\251", "alpha\nno\nalpha last", 19);
    write_file("tail", "alpha\n", 6);
    assert(run(NULL, 3, plain) == 0);
    assert(strcmp(out_bare(), "%s: space\303\251:alpha\n%s: space\303\251:alpha last\ntail:alpha\n") == 0);
    assert(run(NULL, 4, numbered) == 0);
    assert(strcmp(out_bare(), "%s: space\303\251:1:alpha\n%s: space\303\251:3:alpha last\ntail:1:alpha\n") == 0);
    assert(err_text[0] == '\0');
}

static void test_no_match(void)
{
    static const char *const args[] = {"zzz", "@t2"};

    write_file("t2", "no\nalpha here\n", 14);
    assert(run(NULL, 2, args) == 1);
    assert(out_text[0] == '\0' && err_text[0] == '\0');
}

/* An unreadable path is reported, the rest are searched, and the status is
 * 2 even though something matched. */
static void test_missing_path(void)
{
    static const char *const args[] = {"alpha", "@missing", "@t2"};
    char path[512];

    path_of(path, "missing");
    (void)remove(path);
    write_file("t2", "no\nalpha here\n", 14);
    assert(run(NULL, 3, args) == 2);
    assert(strcmp(out_bare(), "t2:alpha here\n") == 0);
    assert(strstr(err_text, "cgrep-test-missing") != NULL);
}

static void test_binary(void)
{
    static const char *const one[] = {"alpha", "@bin"};
    static const char *const nomatch[] = {"zzz", "@bin"};
    static const char *const several[] = {"-n", "alpha", "@bin2", "@bin"};

    write_file("bin", "alpha\nbeta alpha\n\0bin alpha\n", 28);
    write_file("bin2", "x\n\0\nalpha\n", 10);
    assert(run(NULL, 2, one) == 0);
    assert(strcmp(out_text, "binary file matches (found \"\\0\" byte around offset 17)\n") == 0);
    assert(run(NULL, 2, nomatch) == 1);
    assert(out_text[0] == '\0');
    assert(run(NULL, 4, several) == 0);
    assert(strcmp(out_bare(), "bin2: binary file matches (found \"\\0\" byte around offset 2)\n"
                              "bin: binary file matches (found \"\\0\" byte around offset 17)\n") == 0);
}

static void test_lines(void)
{
    static const char *const empty[] = {"", "@nonl"};
    static const char *const nonl[] = {"alpha", "@nonl"};
    static const char *const at_end[] = {"end", "@nonl"};
    static const char *const after_miss[] = {"nd", "@nnd"};
    static const char *const crlf[] = {"-n", "alpha", "@crlf"};

    write_file("nonl", "x\nalpha end", 11);
    write_file("crlf", "one alpha\r\ntwo\r\n", 16);
    assert(run(NULL, 2, empty) == 0);
    assert(strcmp(out_text, "x\nalpha end\n") == 0);
    assert(run(NULL, 2, nonl) == 0);
    assert(strcmp(out_text, "alpha end\n") == 0);
    /* A match ending exactly at the end of the input, found directly and
     * right after a candidate that failed ("nn" is not "nd"). */
    assert(run(NULL, 2, at_end) == 0);
    assert(strcmp(out_text, "alpha end\n") == 0);
    write_file("nnd", "a nnd", 5);
    assert(run(NULL, 2, after_miss) == 0);
    assert(strcmp(out_text, "a nnd\n") == 0);
    assert(run(NULL, 3, crlf) == 0);
    assert(strcmp(out_text, "1:one alpha\r\n") == 0);
}

static void test_stdin(void)
{
    static const char *const numbered[] = {"-n", "alpha"};
    static const char *const plain[] = {"alpha"};

    write_file("in", "a\nalpha\n", 8);
    assert(run("in", 2, numbered) == 0);
    assert(strcmp(out_text, "2:alpha\n") == 0);
    write_file("inbin", "alpha\nbeta alpha\n\0bin alpha\n", 28);
    assert(run("inbin", 1, plain) == 0);
    assert(strcmp(out_text, "binary file matches (found \"\\0\" byte around offset 17)\n") == 0);
}

static void test_boundaries_and_explicit_stdin(void)
{
    static const char *const exact[] = {"a", "@short"};
    static const char *const long_pattern[] = {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa", "@short"};
    static const char *const binary_start[] = {"alpha", "@bin-start"};
    static const char *const leading_blank[] = {"-n", "alpha", "@leading"};
    static const char *const dash_input[] = {"-n", "alpha", "-"};
    static const char *const dash_pattern[] = {"-", "@short"};
    static const char *const escaped_flag[] = {"--", "-n", "@short"};
    static const char *const literal[] = {"a.b", "@dots"};
    static const char *const file_only[] = {"alpha", "@short"};
    static const char *const newline[] = {"a\nb", "@short"};

    write_file("short", "a", 1);
    assert(run(NULL, 2, exact) == 0);
    assert(strcmp(out_text, "a\n") == 0);
    assert(run(NULL, 2, long_pattern) == 1);
    assert(out_text[0] == '\0');
    write_file("bin-start", "alpha\0", 6);
    assert(run(NULL, 2, binary_start) == 0);
    assert(strcmp(out_text, "binary file matches (found \"\\0\" byte around offset 5)\n") == 0);
    write_file("leading", "\nalpha\n", 7);
    assert(run(NULL, 3, leading_blank) == 0);
    assert(strcmp(out_text, "2:alpha\n") == 0);
    write_file("in", "alpha\n", 6);
    assert(run("in", 3, dash_input) == 0);
    assert(strcmp(out_text, "1:alpha\n") == 0);
    /* Explicit file arguments must not consume standard input. */
    assert(run("in", 2, file_only) == 1);
    assert(out_text[0] == '\0');
    write_file("short", "a-b -n\n", 7);
    assert(run(NULL, 2, dash_pattern) == 0);
    assert(strcmp(out_text, "a-b -n\n") == 0);
    assert(run(NULL, 3, escaped_flag) == 0);
    assert(strcmp(out_text, "a-b -n\n") == 0);
    write_file("dots", "axb\na.b\n", 8);
    assert(run(NULL, 2, literal) == 0);
    assert(strcmp(out_text, "a.b\n") == 0);
    assert(run(NULL, 2, newline) == 2);
    assert(out_text[0] == '\0' && strstr(err_text, "newline") != NULL);
}

/* Repeated bytes can make the first, middle, or last byte the selective
 * candidate. False candidates must not hide overlapping or final matches. */
static void test_literal_shapes(void)
{
    static const char *const tail[] = {"aaaaab", "@shape"};
    static const char *const middle[] = {"-n", "aaabaa", "@shape"};
    static const char *const head[] = {"baaaaa", "@shape"};
    static const char *const overlap[] = {"ababac", "@shape"};
    static const char *const rejects[] = {"aaabc", "@shape"};
    static const char *const utf8[] = {"\303\251", "@shape"};

    write_file("shape", "aaaaacaaaaab", 12);
    assert(run(NULL, 2, tail) == 0);
    assert(strcmp(out_text, "aaaaacaaaaab\n") == 0);
    write_file("shape", "aaaaaaa\naaabaa\n", 15);
    assert(run(NULL, 3, middle) == 0);
    assert(strcmp(out_text, "2:aaabaa\n") == 0);
    write_file("shape", "aaaaaa\nbaaaaa\n", 14);
    assert(run(NULL, 2, head) == 0);
    assert(strcmp(out_text, "baaaaa\n") == 0);
    write_file("shape", "ababababac", 10);
    assert(run(NULL, 2, overlap) == 0);
    assert(strcmp(out_text, "ababababac\n") == 0);
    write_file("shape", "ccccccccccccccccccccaaabc", 25);
    assert(run(NULL, 2, rejects) == 0);
    assert(strcmp(out_text, "ccccccccccccccccccccaaabc\n") == 0);
    write_file("shape", "e\n\303\251\n", 5);
    assert(run(NULL, 2, utf8) == 0);
    assert(strcmp(out_text, "\303\251\n") == 0);
}

/* A failed write sets the stream's sticky error flag even when a later
 * fflush returns zero. The CLI must still return its error status. */
static void test_output_error(void)
{
    char path[512];
    char *argv[3];
    FILE *in;
    FILE *out;
    FILE *err;

    write_file("in", "alpha\n", 6);
    path_of(path, "in");
    in = fopen(path, "rb");
    out = fopen(path, "rb");
    err = fopen(path, "ab");
    assert(in != NULL && out != NULL && err != NULL);
    argv[0] = (char *)"cgrep";
    argv[1] = (char *)"alpha";
    argv[2] = NULL;
    assert(cgrep_main(2, argv, in, out, err) == 2);
    assert(ferror(out));
    assert(fclose(in) == 0 && fclose(out) == 0 && fclose(err) == 0);
}

static void test_usage(void)
{
    static const char *const flag_only[] = {"-n"};
    static const char *const unknown[] = {"--no-such-flag", "alpha"};

    assert(run(NULL, 0, flag_only) == 2);
    assert(strstr(err_text, "usage") != NULL);
    assert(run(NULL, 1, flag_only) == 2);
    assert(run(NULL, 2, unknown) == 2);
    assert(strstr(err_text, "--no-such-flag") != NULL && out_text[0] == '\0');
}

int main(void)
{
    const char *tmp;

    tmp = getenv("TMPDIR");
    if (tmp == NULL || tmp[0] == '\0')
        tmp = "/tmp";
    assert(strlen(tmp) < sizeof dir);
    strcpy(dir, tmp);
    test_one_file();
    test_several_files_and_numbers();
    test_literal_path_prefixes();
    test_no_match();
    test_missing_path();
    test_binary();
    test_lines();
    test_stdin();
    test_usage();
    test_boundaries_and_explicit_stdin();
    test_literal_shapes();
    test_output_error();
    return 0;
}
