/* Directory arguments, against expected texts taken from the reference
 * (ripgrep --sort path --no-ignore on the same tree). */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cu.h"
#include "cgrep.h"

static char out_text[8192];
static char err_text[2048];
static char root[512];

static const char *slurp(const char *path, char *buf, size_t cap)
{
    FILE *f;
    size_t n;

    f = fopen(path, "rb");
    assert(f != NULL);
    n = fread(buf, 1, cap - 1, f);
    assert(!ferror(f) && fclose(f) == 0);
    buf[n] = '\0';
    return buf;
}

/* Runs cgrep with args, where "@x" is root/x, and stdin empty. Output with
 * root's prefix removed, so expected texts are relative. */
static int run(int argc, const char *const *args)
{
    char *argv[16];
    char paths[16][600];
    char in_path[600];
    char out_path[600];
    char err_path[600];
    char raw[8192];
    FILE *in;
    FILE *out;
    FILE *err;
    const char *p;
    size_t used;
    size_t n;
    int i;
    int status;

    assert(argc < 15);
    argv[0] = (char *)"cgrep";
    for (i = 0; i < argc; i++) {
        if (args[i][0] == '@') {
            sprintf(paths[i], "%s/%s", root, args[i] + 1);
            argv[i + 1] = paths[i];
        } else {
            argv[i + 1] = (char *)args[i];
        }
    }
    argv[argc + 1] = NULL;
    sprintf(in_path, "%s.in", root);
    sprintf(out_path, "%s.out", root);
    sprintf(err_path, "%s.err", root);
    in = fopen(in_path, "wb");
    assert(in != NULL && fclose(in) == 0);
    in = fopen(in_path, "rb");
    out = fopen(out_path, "wb");
    err = fopen(err_path, "wb");
    assert(in != NULL && out != NULL && err != NULL);
    status = cgrep_main(argc + 1, argv, in, out, err);
    assert(fclose(in) == 0 && fclose(out) == 0 && fclose(err) == 0);
    slurp(out_path, raw, sizeof raw);
    slurp(err_path, err_text, sizeof err_text);
    n = strlen(root);
    used = 0;
    for (p = raw; *p != '\0';) {
        if (strncmp(p, root, n) == 0 && p[n] == '/')
            p += n + 1;
        else
            out_text[used++] = *p++;
    }
    out_text[used] = '\0';
    return status;
}

#ifdef __cerb__
/* Cerberus models no directories (cu_dir reports ENOSYS), so a directory
 * argument is opened as a file and fails; the walk tests run natively and
 * under wasm. */
int main(void)
{
    static const char *const args[] = {"alpha", "."};

    strcpy(root, "/tmp/cgrep-walk");
    assert(run(2, args) == 2 && out_text[0] == '\0' && err_text[0] != '\0');
    printf("test_walk: PASS (no directories on this platform)\n");
    return 0;
}
#else

#include <sys/stat.h>
#include <unistd.h>

static void make_dir(const char *rel)
{
    char p[600];

    sprintf(p, "%s/%s", root, rel);
    assert(mkdir(p, 0700) == 0);
}

static void make_file(const char *rel, const char *data, size_t n)
{
    char p[600];
    FILE *f;

    sprintf(p, "%s/%s", root, rel);
    f = fopen(p, "wb");
    assert(f != NULL && fwrite(data, 1, n, f) == n && fclose(f) == 0);
}

static void remove_tree(void)
{
    static const char *const files[] = {
        "r/A", "r/a/deep/y", "r/a-b", "r/a.txt", "r/b/x", "r/.hid/h", "r/.dot", "r/b/bin", "r/b/link",
        "r/b/bin2", "r/locked/x"};
    static const char *const dirs[] = {"r/a/deep", "r/a", "r/b", "r/.hid", "r/empty", "r/locked", "r"};
    char p[600];
    size_t i;

    for (i = 0; i < sizeof files / sizeof files[0]; i++) {
        sprintf(p, "%s/%s", root, files[i]);
        remove(p);
    }
    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        sprintf(p, "%s/%s", root, dirs[i]);
        chmod(p, 0700);
        remove(p);
    }
    remove(root);
    sprintf(p, "%s.in", root);
    remove(p);
    sprintf(p, "%s.out", root);
    remove(p);
    sprintf(p, "%s.err", root);
    remove(p);
}

int main(void)
{
    static const char *const plain[] = {"alpha", "@r"};
    static const char *const numbered[] = {"-n", "alpha", "@r/a"};
    static const char *const counted[] = {"-c", "alpha", "@r"};
    static const char *const listed[] = {"-l", "alpha", "@r"};
    static const char *const inverted[] = {"-v", "alpha", "@r"};
    static const char *const binary[] = {"al", "@r/b"};
    static const char *const slash[] = {"alpha", "@r/b/"};
    static const char *const binary_count[] = {"-c", "al", "@r/b"};
    static const char *const empty[] = {"alpha", "@r/empty"};
    static const char *const mixed[] = {"alpha", "@r/a.txt", "@r/a"};
    static const char *const workers[] = {"-j4", "alpha", "@r"};
    static const char *const locked[] = {"alpha", "@r/a", "@r/locked"};
    static const char *const binary_list[] = {"-l", "al", "@r/b"};
    const char *tmp;
    char p[600];
    int listable;

    tmp = getenv("TMPDIR");
    if (tmp == NULL || tmp[0] == '\0')
        tmp = "/tmp";
    assert(strlen(tmp) + 32 < sizeof root);
    /* A fixed name (WASI has no process ids); a leftover tree is removed. */
    sprintf(root, "%s/cgrep-walk", tmp);
    remove_tree();
    assert(mkdir(root, 0700) == 0);
    make_dir("r");
    make_dir("r/a");
    make_dir("r/a/deep");
    make_dir("r/b");
    make_dir("r/.hid");
    make_dir("r/empty");
    make_file("r/A", "alpha\n", 6);
    make_file("r/a/deep/y", "alpha one\n", 10);
    make_file("r/a-b", "x\nalpha\n", 8);
    make_file("r/a.txt", "alpha\nbeta\n", 11);
    make_file("r/b/x", "alpha\n", 6);
    make_file("r/.hid/h", "alpha\n", 6);
    make_file("r/.dot", "alpha\n", 6);
    /* The first 3-byte refill holds "al\n"; the next one has the NUL. */
    make_file("r/b/bin", "al\nalpha\0\n", 10);
    /* Its first line ends in the refill with the NUL, so it never counts. */
    make_file("r/b/bin2", "alphabet\0\n", 10);
    sprintf(p, "%s/r/b/link", root);
    assert(symlink("../a.txt", p) == 0);

    /* Byte order of names (A < a < a-b < a.txt), depth first; hidden
     * names and links skipped; the binary file stops before "alpha". */
    assert(run(2, plain) == 0);
    assert(strcmp(out_text, "r/A:alpha\nr/a/deep/y:alpha one\nr/a-b:alpha\nr/a.txt:alpha\nr/b/x:alpha\n") == 0);
    assert(err_text[0] == '\0');
    assert(run(3, numbered) == 0);
    assert(strcmp(out_text, "r/a/deep/y:1:alpha one\n") == 0);
    assert(run(3, counted) == 0);
    assert(strcmp(out_text, "r/A:1\nr/a/deep/y:1\nr/a-b:1\nr/a.txt:1\nr/b/x:1\n") == 0);
    assert(run(3, listed) == 0);
    assert(strcmp(out_text, "r/A\nr/a/deep/y\nr/a-b\nr/a.txt\nr/b/x\n") == 0);
    /* A walked binary file warns only after printed lines, with its own
     * wording, and is dropped from counts. */
    assert(run(3, inverted) == 0);
    assert(strcmp(out_text, "r/a-b:x\nr/a.txt:beta\nr/b/bin:al\nr/b/bin: WARNING: stopped searching binary file "
                            "after match (found \"\\0\" byte around offset 8)\n") == 0);
    assert(run(2, binary) == 0);
    assert(strcmp(out_text, "r/b/bin:al\nr/b/bin: WARNING: stopped searching binary file after match "
                            "(found \"\\0\" byte around offset 8)\nr/b/x:alpha\n") == 0);
    assert(run(3, binary_list) == 0);
    assert(strcmp(out_text, "r/b/bin\nr/b/x\n") == 0);
    assert(run(3, binary_count) == 0);
    assert(strcmp(out_text, "r/b/x:1\n") == 0);
    assert(run(2, slash) == 0);
    assert(strcmp(out_text, "r/b/x:alpha\n") == 0);
    assert(run(2, empty) == 1 && out_text[0] == '\0' && err_text[0] == '\0');
    assert(run(3, mixed) == 0);
    assert(strcmp(out_text, "r/a.txt:alpha\nr/a/deep/y:alpha one\n") == 0);
    assert(run(3, workers) == 0);
    assert(strcmp(out_text, "r/A:alpha\nr/a/deep/y:alpha one\nr/a-b:alpha\nr/a.txt:alpha\nr/b/x:alpha\n") == 0);
#ifndef __wasi__
    /* An unreadable directory is reported, the rest still searched. */
    make_dir("r/locked");
    make_file("r/locked/x", "alpha\n", 6);
    sprintf(p, "%s/r/locked", root);
    assert(chmod(p, 0) == 0);
    {
        CuArena a;
        CuDir d;

        cu_arena_init(&a);
        listable = cu_dir_open(&d, &a, p);
        if (listable)
            cu_dir_close(&d);
        cu_arena_release(&a);
    }
    if (!listable) {
        assert(run(3, locked) == 2);
        assert(strcmp(out_text, "r/a/deep/y:alpha one\n") == 0);
        assert(strstr(err_text, "locked") != NULL && strstr(err_text, strerror(EACCES)) != NULL);
        /* Found while walking: the same report, the rest still searched. */
        assert(run(2, plain) == 2);
        assert(strcmp(out_text, "r/A:alpha\nr/a/deep/y:alpha one\nr/a-b:alpha\nr/a.txt:alpha\nr/b/x:alpha\n") == 0);
        assert(strstr(err_text, "locked") != NULL && strstr(err_text, strerror(EACCES)) != NULL);
    }
#else
    (void)locked;
    (void)listable;
#endif
    remove_tree();
    printf("test_walk: PASS\n");
    return 0;
}
#endif
