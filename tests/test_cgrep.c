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

/* -v, -c and -l, alone, clustered and combined, against expected texts
 * taken from the reference. */
static void test_selection_flags(void)
{
    static const char *const invert[] = {"-vn", "alpha", "@sa", "@sb"};
    static const char *const invert_tail[] = {"-v", "alpha", "@stail"};
    static const char *const count[] = {"-c", "alpha", "@sa", "@sb"};
    static const char *const count_one[] = {"-c", "alpha", "@sa"};
    static const char *const count_invert[] = {"-cv", "alpha", "@sa", "@sb"};
    static const char *const files[] = {"-l", "alpha", "@sa", "@sb"};
    static const char *const files_invert[] = {"-lv", "alpha", "@sa", "@sb"};
    static const char *const files_stdin[] = {"-l", "alpha"};
    static const char *const count_then_files[] = {"-cl", "alpha", "@sa"};
    static const char *const files_then_count[] = {"-l", "-c", "alpha", "@sa"};
    static const char *const count_binary[] = {"-c", "alpha", "@sbin"};
    static const char *const files_binary[] = {"-l", "alpha", "@sbin"};
    static const char *const invert_binary[] = {"-v", "alpha", "@sbin"};
    static const char *const none[] = {"-c", "zzz", "@sa", "@sb"};
    static const char *const invert_all[] = {"-v", "", "@sa"};
    static const char *const count_empty[] = {"-c", "", "@stail"};
    static const char *const invert_run[] = {"-v", "alpha", "@srun"};
    static const char *const invert_run_numbered[] = {"-vn", "alpha", "@srun"};

    write_file("sa", "alpha one\nbeta\nalpha two\n\ngamma", 31);
    write_file("sb", "alpha\n", 6);
    write_file("stail", "x\nalpha\nlast", 12);
    write_file("sbin", "alpha\0alpha\nbeta\nalpha\n", 23);
    write_file("srun", "alpha\nb1\nb2\nb3\nalpha\nc1\nc2", 26);
    assert(run(NULL, 4, invert) == 0);
    assert(strcmp(out_bare(), "sa:2:beta\nsa:4:\nsa:5:gamma\n") == 0);
    assert(run(NULL, 3, invert_tail) == 0);
    assert(strcmp(out_text, "x\nlast\n") == 0);
    assert(run(NULL, 4, count) == 0);
    assert(strcmp(out_bare(), "sa:2\nsb:1\n") == 0);
    assert(run(NULL, 3, count_one) == 0);
    assert(strcmp(out_text, "2\n") == 0);
    assert(run(NULL, 4, count_invert) == 0);
    assert(strcmp(out_bare(), "sa:3\n") == 0);
    assert(run(NULL, 4, files) == 0);
    assert(strcmp(out_bare(), "sa\nsb\n") == 0);
    assert(run(NULL, 4, files_invert) == 0);
    assert(strcmp(out_bare(), "sa\n") == 0);
    assert(run("sb", 2, files_stdin) == 0);
    assert(strcmp(out_text, "<stdin>\n") == 0);
    assert(run(NULL, 3, count_then_files) == 0);
    assert(strcmp(out_bare(), "sa\n") == 0);
    assert(run(NULL, 4, files_then_count) == 0);
    assert(strcmp(out_text, "2\n") == 0);
    /* NUL ends lines for -c and -l; printing warns instead. */
    assert(run(NULL, 3, count_binary) == 0);
    assert(strcmp(out_text, "3\n") == 0);
    assert(run(NULL, 3, files_binary) == 0);
    assert(strcmp(out_bare(), "sbin\n") == 0);
    assert(run(NULL, 3, invert_binary) == 0);
    assert(strcmp(out_text, "binary file matches (found \"\\0\" byte around offset 5)\n") == 0);
    assert(run(NULL, 4, none) == 1 && out_text[0] == '\0');
    assert(run(NULL, 3, invert_all) == 1 && out_text[0] == '\0');
    /* Adjacent unselected-line runs inside one window, and at its end. */
    assert(run(NULL, 3, invert_run) == 0);
    assert(strcmp(out_text, "b1\nb2\nb3\nc1\nc2\n") == 0);
    assert(run(NULL, 3, invert_run_numbered) == 0);
    assert(strcmp(out_text, "2:b1\n3:b2\n4:b3\n6:c1\n7:c2\n") == 0);
    assert(run(NULL, 3, count_empty) == 0);
    assert(strcmp(out_text, "3\n") == 0);
    assert(err_text[0] == '\0');
}

/* -i and -w against expected texts taken from the reference. Bytes from
 * 0x80 are word bytes, so a non-ASCII letter blocks a word match. */
static void test_case_and_words(void)
{
    static const char *const empty_word[] = {"-w", "", "@w1"};
    static const char *const empty_word_numbered[] = {"-wn", "", "@w1"};
    static const char *const dash[] = {"-w", "-", "@w1"};
    static const char *const space_after[] = {"-w", "a ", "@w1"};
    static const char *const letter[] = {"-w", "b", "@w1"};
    static const char *const word_count[] = {"-wc", "a", "@w1"};
    static const char *const word_invert[] = {"-wv", "a", "@w1"};
    static const char *const folded[] = {"-i", "A B", "@w1"};
    static const char *const folded_word[] = {"-iw", "A", "@w1"};
    static const char *const retry[] = {"-w", "aa", "@w2"};
    static const char *const unicode[] = {"-w", "foo", "@w3"};
    static const char *const folded_count[] = {"-ic", "AA", "@w4"};
    static const char *const bracket[] = {"-i", "[@", "@w5"};
    static const char *const brace[] = {"-i", "{`", "@w5"};

    write_file("w1", "a b\n\nab\n-\nx-y\na_b\n", 18);
    write_file("w2", "xaay aa\naaa aa\naaaa\n", 20);
    write_file("w3", "\303\251foo foo\n\303\251foo\nfoo_\n", 21);
    write_file("w4", "aA b\nAa\nx\n", 10);
    assert(run(NULL, 3, empty_word) == 0);
    assert(strcmp(out_text, "\n-\n") == 0);
    assert(run(NULL, 3, empty_word_numbered) == 0);
    assert(strcmp(out_text, "2:\n4:-\n") == 0);
    assert(run(NULL, 3, dash) == 0);
    assert(strcmp(out_text, "-\n") == 0);
    assert(run(NULL, 3, space_after) == 1 && out_text[0] == '\0');
    assert(run(NULL, 3, letter) == 0);
    assert(strcmp(out_text, "a b\n") == 0);
    assert(run(NULL, 3, word_count) == 0);
    assert(strcmp(out_text, "1\n") == 0);
    assert(run(NULL, 3, word_invert) == 0);
    assert(strcmp(out_text, "\nab\n-\nx-y\na_b\n") == 0);
    assert(run(NULL, 3, folded) == 0);
    assert(strcmp(out_text, "a b\n") == 0);
    assert(run(NULL, 3, folded_word) == 0);
    assert(strcmp(out_text, "a b\n") == 0);
    /* A later candidate in the line, after earlier ones fail their bounds. */
    assert(run(NULL, 3, retry) == 0);
    assert(strcmp(out_text, "xaay aa\naaa aa\n") == 0);
    assert(run(NULL, 3, unicode) == 0);
    assert(strcmp(out_text, "\303\251foo foo\n") == 0);
    assert(run(NULL, 3, folded_count) == 0);
    assert(strcmp(out_text, "2\n") == 0);
    /* '@' and '[' border A-Z: folding leaves them, in the vector path too. */
    write_file("w5", "0123456789abcdef[@\n0123456789abcdef{`\n", 38);
    assert(run(NULL, 3, bracket) == 0);
    assert(strcmp(out_text, "0123456789abcdef[@\n") == 0);
    assert(run(NULL, 3, brace) == 0);
    assert(strcmp(out_text, "0123456789abcdef{`\n") == 0);
    assert(err_text[0] == '\0');
}

/* Numbered lines whose prefix and body together straddle the CLI's
 * 256-byte output stage, one body longer than the stage, and a final
 * line without LF, whose newline is a separate part. */
static void test_staged_output(void)
{
    static const char *const args[] = {"-n", "alpha", "@staged"};
    static char input[8192];
    static char expected[8192];
    size_t in_len;
    size_t ex_len;
    size_t body;
    size_t i;
    unsigned long line;

    in_len = 0;
    ex_len = 0;
    line = 0;
    for (body = 245; body <= 263; body = body == 262 ? 600 : body + 1) {
        line++;
        ex_len += (size_t)sprintf(expected + ex_len, "%lu:", line);
        memcpy(input + in_len, "alpha", 5);
        for (i = 5; i < body; i++)
            input[in_len + i] = (char)('a' + i % 23);
        input[in_len + body] = '\n';
        memcpy(expected + ex_len, input + in_len, body + 1);
        in_len += body + 1;
        ex_len += body + 1;
        if (body == 600)
            break;
    }
    memcpy(input + in_len, "alpha end", 9);
    in_len += 9;
    ex_len += (size_t)sprintf(expected + ex_len, "%lu:alpha end\n", line + 1);
    write_file("staged", input, in_len);
    assert(run(NULL, 3, args) == 0);
    assert(strlen(out_text) == ex_len && memcmp(out_text, expected, ex_len) == 0);
    assert(err_text[0] == '\0');
}

/* Builds without make's revision define report "unknown". */
static void test_version(void)
{
    static const char *const long_flag[] = {"--version"};
    static const char *const short_flag[] = {"-n", "-V", "alpha", "@missing"};
    static const char prefix[] = "cgrep " CGREP_VERSION " (";
    size_t n;

    assert(run(NULL, 1, long_flag) == 0);
    n = strlen(out_text);
    assert(strncmp(out_text, prefix, sizeof prefix - 1) == 0);
    assert(n > sizeof prefix && strcmp(out_text + n - 2, ")\n") == 0);
    assert(err_text[0] == '\0');
    assert(run(NULL, 4, short_flag) == 0);
    assert(strncmp(out_text, prefix, sizeof prefix - 1) == 0 && err_text[0] == '\0');
}

static void test_new_options(void)
{
    static const char *const help_long[] = {"--help"};
    static const char *const help_short[] = {"-h"};
    static const char *const invalid[] = {"--max-depth", ""};
    static const char *const negative[] = {"--max-depth=-1", "alpha"};
    static const char *const overflow[] = {"--max-depth=999999999999999999999999", "alpha"};
    static const char *const junk[] = {"--max-depth=1x", "alpha"};
    static const char *const missing[] = {"--max-depth"};
    static const char *const valid[] = {"--max-depth=+0", "alpha"};

    assert(run(NULL, 1, help_long) == 0 && err_text[0] == '\0');
    assert(strncmp(out_text, "usage: cgrep", 12) == 0);
    assert(strstr(out_text, "--hidden") != NULL);
    assert(strstr(out_text, "1 through 32") != NULL);
    assert(run(NULL, 1, help_short) == 0 && err_text[0] == '\0');
    assert(strstr(out_text, "--max-depth N") != NULL);
    assert(run(NULL, 2, invalid) == 2 && out_text[0] == '\0');
    assert(run(NULL, 2, negative) == 2);
    assert(run(NULL, 2, overflow) == 2);
    assert(run(NULL, 2, junk) == 2);
    assert(run(NULL, 1, missing) == 2);
    assert(run(NULL, 2, valid) == 1 && err_text[0] == '\0');
}

/* The reference's long names behave as their short forms. */
static void test_long_names(void)
{
    static const char *const pairs[][2] = {
        {"--line-number", "-n"}, {"--ignore-case", "-i"}, {"--word-regexp", "-w"},
        {"--invert-match", "-v"}, {"--count", "-c"}, {"--files-with-matches", "-l"}
    };
    static const char text[] = "alpha\nALPHA x\nalphabet\nbeta\n";
    static const char input[] = "names";
    static const char *const threads_long[] = {"--threads", "2", "-c", "alpha"};
    static const char *const threads_joined[] = {"--threads=2", "-c", "alpha"};
    static const char *const threads_short[] = {"-j2", "-c", "alpha"};
    static const char *const threads_zero[] = {"--threads=0", "alpha"};
    static const char *const depth_short[] = {"-d", "0", "alpha"};
    static const char *const depth_joined[] = {"-nd0", "alpha"};
    static const char *const depth_bad[] = {"-d1x", "alpha"};
    static const char *const depth_missing[] = {"-d"};
    const char *args[2];
    char expected[sizeof out_text];
    int status;
    size_t k;

    write_file(input, text, sizeof text - 1);
    for (k = 0; k < sizeof pairs / sizeof pairs[0]; k++) {
        args[0] = pairs[k][1];
        args[1] = "alpha";
        status = run(input, 2, args);
        strcpy(expected, out_text);
        args[0] = pairs[k][0];
        assert(run(input, 2, args) == status && strcmp(out_text, expected) == 0);
        assert(err_text[0] == '\0');
    }
    assert(run(input, 3, threads_short) == 0);
    strcpy(expected, out_text);
    assert(run(input, 4, threads_long) == 0 && strcmp(out_text, expected) == 0);
    assert(run(input, 3, threads_joined) == 0 && strcmp(out_text, expected) == 0);
    assert(run(input, 2, threads_zero) == 2 && strstr(err_text, "worker count") != NULL);
    assert(run(input, 3, depth_short) == 0 && strcmp(out_text, "alpha\nalphabet\n") == 0);
    assert(run(input, 2, depth_joined) == 0 && strcmp(out_text, "1:alpha\n3:alphabet\n") == 0);
    assert(run(input, 2, depth_bad) == 2 && strstr(err_text, "max depth") != NULL);
    assert(run(input, 1, depth_missing) == 2);
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
    test_long_names();
    test_usage();
    test_version();
    test_new_options();
#ifndef __cerb__
    /* Under Cerberus these run in sem_flags.c, within their own time limit. */
    test_staged_output();
    test_selection_flags();
    test_case_and_words();
#endif
    test_boundaries_and_explicit_stdin();
    test_literal_shapes();
    test_output_error();
    return 0;
}
