/* Directory arguments, against expected texts taken from the reference
 * (ripgrep --sort path, with --no-ignore for the unfiltered cases). */
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

/* h/a/a/.../a (CHAIN_DEPTH deep) ending in a file f, for ignore_work. */
#define CHAIN_DEPTH 40
/* Ordinary rules in one file, more than a verdict's base budget covers. */
#define RULE_COUNT 50000

static void chain_path(char *p, int depth, const char *leaf)
{
    int i;

    strcpy(p, "h");
    for (i = 0; i < depth; i++)
        strcat(p, "/a");
    if (leaf != NULL) {
        strcat(p, "/");
        strcat(p, leaf);
    }
}

static void remove_chain(void)
{
    char rel[128];
    char p[700];
    int depth;

    chain_path(rel, CHAIN_DEPTH, "f");
    sprintf(p, "%s/%s", root, rel);
    remove(p);
    for (depth = CHAIN_DEPTH; depth >= 0; depth--) {
        chain_path(rel, depth, NULL);
        sprintf(p, "%s/%s", root, rel);
        remove(p);
    }
    sprintf(p, "%s/h/.ignore", root);
    remove(p);
    sprintf(p, "%s/h", root);
    remove(p);
}

static void remove_tree(void)
{
    static const char *const files[] = {
        "r/A", "r/a/deep/y", "r/a-b", "r/a.txt", "r/b/x", "r/.hid/h", "r/.dot", "r/b/bin", "r/b/link",
        "r/b/bin2", "r/locked/x",
        "g/a.log", "g/keep.log", "g/b.txt", "g/c.tmp", "g/x.ex", "g/.shown", "g/.secret", "g/sub/c.log",
        "g/sub/y.txt", "g/sub/z.txt", "g/sub/deep/d.txt", "g/sub/.kept", "g/out/o.txt", "g/.gitignore", "g/.ignore", "g/sub/.ignore",
        "g/sub/.rgignore", "g/.git/info/exclude", "xdg/git/ignore", "xdg/.config/git/ignore", "g/lib/keep.txt", "g/lib/other.txt",
        "e/.ignore", "e/#hash", "e/!bang", "e/space ", "e/trim", "e/trim ", "e/star*", "e/foo",
        "e/keep", "e/x", "e/sub/x", "e/sub/deep/x", "e/sub/foo", "e/sub/]", "e/sub/[", "e/sub/end\n", "e/sub/f\\o",
        "e/blocked/keep", "e/tail/keep",
        "w/common/info/exclude", "w/common/worktrees/wt/commondir", "w/common/worktrees/wt/info/exclude",
        "w/tree/.git", "w/tree/shared", "w/tree/own", "w/tree/kept", "v/bin", "v/text"};
    static const char *const dirs[] = {"r/a/deep", "r/a", "r/b", "r/.hid", "r/empty", "r/locked", "r",
        "g/sub/deep", "g/sub", "g/out", "g/lib", "g/.git/info", "g/.git", "g", "xdg/git",
        "xdg/.config/git", "xdg/.config", "xdg",
        "e/sub/deep", "e/sub", "e/blocked", "e/tail", "e",
        "w/common/worktrees/wt/info", "w/common/worktrees/wt", "w/common/worktrees", "w/common/info",
        "w/common", "w/tree", "w", "v"};
    char p[600];
    size_t i;

    /* Permissions first: a failed run can leave r/locked unreadable. */
    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        sprintf(p, "%s/%s", root, dirs[i]);
        chmod(p, 0700);
    }
    for (i = 0; i < sizeof files / sizeof files[0]; i++) {
        sprintf(p, "%s/%s", root, files[i]);
        remove(p);
    }
    for (i = 0; i < sizeof dirs / sizeof dirs[0]; i++) {
        sprintf(p, "%s/%s", root, dirs[i]);
        remove(p);
    }
    remove_chain();
    remove(root);
    sprintf(p, "%s.in", root);
    remove(p);
    sprintf(p, "%s.out", root);
    remove(p);
    sprintf(p, "%s.err", root);
    remove(p);
}

/* Ignore files, against the reference on the same tree. The tree is its own
 * repository, and the global file is the test's, whatever the machine. */
static void ignore_files(void)
{
    static const char *const whole[] = {"alpha", "@g"};
    static const char *const below[] = {"alpha", "@g/sub"};
    static const char *const unfiltered[] = {"--no-ignore", "alpha", "@g/sub"};
    static const char *const counted[] = {"-c", "alpha", "@g"};
    static const char *const explicit_file[] = {"alpha", "@g/a.log"};
    static const char *const workers[] = {"-j8", "alpha", "@g"};
#ifndef __wasi__
    static const char *const relative[] = {"alpha", "g"};
    static const char *const dot[] = {"alpha", "."};
    char cwd[4096];
    char home[600];
#endif
    static const char parent_rules[] = "sub/deep/*.txt\n!sub/.kept\n";
    static const char expected[] = "g/.shown:alpha\ng/b.txt:alpha\ng/keep.log:alpha\ng/lib/keep.txt:alpha\n"
        "g/sub/y.txt:alpha\ng/sub/z.txt:alpha\n";
    static const char *const names[] = {"g/a.log", "g/keep.log", "g/b.txt", "g/c.tmp", "g/x.ex", "g/.shown",
        "g/.secret", "g/sub/c.log", "g/sub/y.txt", "g/sub/z.txt", "g/sub/deep/d.txt", "g/out/o.txt",
        "g/lib/keep.txt", "g/lib/other.txt"};
    size_t i;

    make_dir("g");
    make_dir("g/.git");
    make_dir("g/.git/info");
    make_dir("g/sub");
    make_dir("g/sub/deep");
    make_dir("g/out");
    make_dir("g/lib");
    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        make_file(names[i], "alpha\n", 6);
    /* A trailing "**" covers what is inside lib, not lib itself, so a
     * later line can un-ignore a file there. */
    make_file("g/.gitignore", "*.log\n!keep.log\nout/\n!.shown\nlib/**\n!lib/keep.txt\n", 50);
    make_file("g/sub/.ignore", "deep\ny.txt\n", 11);
    /* .rgignore outranks .ignore. */
    make_file("g/sub/.rgignore", "!y.txt\n", 7);
    make_file("g/.git/info/exclude", "*.ex\n", 5);
    make_file("xdg/git/ignore", "*.tmp\n", 6);
    assert(run(2, whole) == 0);
    assert(strcmp(out_text, expected) == 0);
    /* The parent's .gitignore still applies below it. */
    assert(run(2, below) == 0);
    assert(strcmp(out_text, "g/sub/y.txt:alpha\ng/sub/z.txt:alpha\n") == 0);
    assert(run(3, unfiltered) == 0);
    assert(strcmp(out_text, "g/sub/c.log:alpha\ng/sub/deep/d.txt:alpha\ng/sub/y.txt:alpha\ng/sub/z.txt:alpha\n") == 0);
    assert(run(3, counted) == 0);
    assert(strcmp(out_text, "g/.shown:1\ng/b.txt:1\ng/keep.log:1\ng/lib/keep.txt:1\ng/sub/y.txt:1\ng/sub/z.txt:1\n") == 0);
    /* An explicit file is never filtered. */
    assert(run(2, explicit_file) == 0);
    assert(strcmp(out_text, "alpha\n") == 0);
    assert(run(3, workers) == 0);
    assert(strcmp(out_text, expected) == 0 && err_text[0] == '\0');
#ifndef __wasi__
    /* An unreadable .gitignore, exclude or global file is skipped without a
     * diagnostic, as the reference does (ripgrep 14.1.1 drops the error). */
    {
        static const char *const locked[] = {"g/.gitignore", "g/.git/info/exclude", "xdg/git/ignore"};
        char p[600];
        FILE *f;

        sprintf(p, "%s/%s", root, locked[0]);
        assert(chmod(p, 0) == 0);
        f = fopen(p, "rb");
        if (f != NULL) {
            assert(fclose(f) == 0);
        } else {
            assert(errno == EACCES);
            for (i = 1; i < sizeof locked / sizeof locked[0]; i++) {
                sprintf(p, "%s/%s", root, locked[i]);
                assert(chmod(p, 0) == 0);
            }
            assert(run(2, whole) == 0 && err_text[0] == '\0');
            assert(strcmp(out_text, "g/a.log:alpha\ng/b.txt:alpha\ng/c.tmp:alpha\ng/keep.log:alpha\n"
                                    "g/lib/keep.txt:alpha\ng/lib/other.txt:alpha\ng/out/o.txt:alpha\n"
                                    "g/sub/c.log:alpha\ng/sub/y.txt:alpha\ng/sub/z.txt:alpha\ng/x.ex:alpha\n") == 0);
        }
        for (i = 0; i < sizeof locked / sizeof locked[0]; i++) {
            sprintf(p, "%s/%s", root, locked[i]);
            assert(chmod(p, 0600) == 0);
        }
        assert(run(2, whole) == 0 && strcmp(out_text, expected) == 0 && err_text[0] == '\0');
    }
    assert(getcwd(cwd, sizeof cwd) != NULL && chdir(root) == 0);
    assert(run(2, relative) == 0 && strcmp(out_text, expected) == 0 && err_text[0] == '\0');
    assert(chdir(cwd) == 0);
    /* Empty XDG_CONFIG_HOME falls back to HOME/.config/git/ignore. */
    make_dir("xdg/.config");
    make_dir("xdg/.config/git");
    make_file("xdg/.config/git/ignore", "*.tmp\n", 6);
    sprintf(home, "%s/xdg", root);
    assert(setenv("HOME", home, 1) == 0 && setenv("XDG_CONFIG_HOME", "", 1) == 0);
    assert(run(2, whole) == 0 && strcmp(out_text, expected) == 0 && err_text[0] == '\0');
    assert(setenv("XDG_CONFIG_HOME", home, 1) == 0);
#endif
    /* An ancestor's anchored rule sees the whole path below the root,
     * including intermediate directories and leading dots in names. */
    make_file("g/.ignore", parent_rules, sizeof parent_rules - 1);
    make_file("g/sub/.ignore", "", 0);
    make_file("g/sub/.rgignore", "", 0);
    make_file("g/sub/.kept", "alpha\n", 6);
    assert(run(2, below) == 0 && err_text[0] == '\0');
    assert(strcmp(out_text, "g/sub/.kept:alpha\ng/sub/y.txt:alpha\ng/sub/z.txt:alpha\n") == 0);
#ifndef __wasi__
    sprintf(home, "%s/g/sub", root);
    assert(chdir(home) == 0);
    assert(run(2, dot) == 0 && err_text[0] == '\0');
    assert(strcmp(out_text, "./.kept:alpha\n./y.txt:alpha\n./z.txt:alpha\n") == 0);
    assert(chdir(cwd) == 0);
#endif
}

/* Escapes and CRLF are parsed from bytes, not the host's text mode.
 * A pruned directory cannot be reopened by a later child negation. */
static void ignore_edges(void)
{
    static const char *const args[] = {"alpha", "@e"};
    static const char *const workers[] = {"-j8", "alpha", "@e"};
    static const char *const names[] = {"e/#hash", "e/!bang", "e/space ", "e/trim", "e/trim ", "e/star*",
        "e/foo", "e/keep", "e/x", "e/sub/x", "e/sub/deep/x", "e/sub/foo", "e/blocked/keep", "e/tail/keep"};
    static const char rules[] = "# comment\r\n\\#hash\r\n\\!bang\r\nspace\\ \r\ntrim   \r\nstar\\*\r\n"
        "foo/\r\n**/x\r\nblocked/\r\n!blocked/keep\r\ntail/**\r\n";
    static const char expected[] = "e/foo:alpha\ne/keep:alpha\ne/sub/foo:alpha\ne/trim :alpha\n";
    size_t i;
#ifndef __wasi__
    char p[600];
    FILE *f;
#endif

    make_dir("e");
    make_dir("e/sub");
    make_dir("e/sub/deep");
    make_dir("e/blocked");
    make_dir("e/tail");
    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        make_file(names[i], "alpha\n", 6);
    make_file("e/.ignore", rules, sizeof rules - 1);
    assert(run(2, args) == 0 && strcmp(out_text, expected) == 0 && err_text[0] == '\0');
    assert(run(3, workers) == 0 && strcmp(out_text, expected) == 0 && err_text[0] == '\0');
    /* Zero and one intermediate components of an anchored **. */
    make_file("e/.ignore", "sub/**/x", 8);
    assert(run(2, args) == 0);
    assert(strstr(out_text, "e/x:alpha\n") != NULL);
    assert(strstr(out_text, "e/sub/x:") == NULL && strstr(out_text, "e/sub/deep/x:") == NULL);
#ifndef __wasi__
    /* ripgrep skips an unreadable ignore file without a diagnostic. */
    sprintf(p, "%s/e/.ignore", root);
    assert(chmod(p, 0) == 0);
    f = fopen(p, "rb");
    if (f != NULL) {
        assert(fclose(f) == 0);
    } else {
        assert(errno == EACCES);
        assert(run(2, args) == 0 && err_text[0] == '\0');
        assert(strstr(out_text, "e/sub/x:alpha\n") != NULL);
        assert(strstr(out_text, "e/sub/deep/x:alpha\n") != NULL);
    }
    assert(chmod(p, 0600) == 0);
#endif
}

/* A .git file names the git directory, relative to its own directory; a
 * commondir there names the one linked worktrees share. info/exclude is read
 * from the shared one, else from the git directory, as git does. */
static void worktrees(void)
{
    static const char *const args[] = {"-l", "alpha", "@w/tree"};
    static const char *const workers[] = {"-j8", "-l", "alpha", "@w/tree"};
    static const char *const names[] = {"w/tree/shared", "w/tree/own", "w/tree/kept"};
    static const char relative[] = "gitdir: ../common/worktrees/wt\n";
    char p[600];
    size_t i;

    make_dir("w");
    make_dir("w/tree");
    make_dir("w/common");
    make_dir("w/common/info");
    make_dir("w/common/worktrees");
    make_dir("w/common/worktrees/wt");
    make_dir("w/common/worktrees/wt/info");
    for (i = 0; i < sizeof names / sizeof names[0]; i++)
        make_file(names[i], "alpha\n", 6);
    make_file("w/common/info/exclude", "shared\n", 7);
    make_file("w/common/worktrees/wt/info/exclude", "own\n", 4);
    make_file("w/common/worktrees/wt/commondir", "../..\r\n", 7);
    make_file("w/tree/.git", relative, sizeof relative - 1);
    assert(run(3, args) == 0 && err_text[0] == '\0');
    assert(strcmp(out_text, "w/tree/kept\nw/tree/own\n") == 0);
    assert(run(4, workers) == 0 && strcmp(out_text, "w/tree/kept\nw/tree/own\n") == 0);
    /* Without commondir, as for a submodule: the git directory's own. */
    sprintf(p, "%s/w/common/worktrees/wt/commondir", root);
    assert(remove(p) == 0);
    sprintf(p, "gitdir: %s/w/common/worktrees/wt", root);
    make_file("w/tree/.git", p, strlen(p));
    assert(run(3, args) == 0 && strcmp(out_text, "w/tree/kept\nw/tree/shared\n") == 0);
    /* Not a gitdir line: no exclude file. */
    make_file("w/tree/.git", "gitdir:x\n", 9);
    assert(run(3, args) == 0 && strcmp(out_text, "w/tree/kept\nw/tree/own\nw/tree/shared\n") == 0);
}

/* Reference paths for bracket boundaries and wildcard backtracking.
 * Malformed-pattern diagnostics are not implemented. */
static void ignore_globs(void)
{
    static const char *const args[] = {"alpha", "@e"};
    static const struct {
        const char *glob;
        const char *name;
    } cases[] = {
        {"f[op]o", "foo"}, {"f[a-o]o", "foo"}, {"f[o-o]o", "foo"}, {"f[!a-n]o", "foo"}, {"f[^a-n]o", "foo"},
        {"f[]o]o", "foo"}, {"f[\\o]o", "foo"}, {"f[\\a-o]o", "foo"}, {"f[-o]o", "foo"},
        {"f[o-]o", "foo"}, {"f?o", "foo"}, {"f*o", "foo"}, {"f**o", "foo"}, {"f\\oo", "foo"},
        {"[]]", "]"}, {"[[]", "["}, {"end*", "end\n"}, {"f[!o]o", NULL}, {"f[a-n]o", NULL}, {"f[z-a]o", NULL}};
    static const char bracket_rules[] = "*\n!sub/\n!sub/f[\\o]o\n";
    char rules[128];
    char expected[128];
    size_t i;
    int status;

    make_file("e/sub/]", "alpha\n", 6);
    make_file("e/sub/[", "alpha\n", 6);
    make_file("e/sub/end\n", "alpha\n", 6);
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        sprintf(rules, "*\n!sub/\n!sub/%s\n", cases[i].glob);
        make_file("e/.ignore", rules, strlen(rules));
        status = run(2, args);
        assert(err_text[0] == '\0');
        if (cases[i].name == NULL) {
            assert(status == 1 && out_text[0] == '\0');
        } else {
            sprintf(expected, "e/sub/%s:alpha\n", cases[i].name);
            assert(status == 0 && strcmp(out_text, expected) == 0);
        }
    }
    make_file("e/sub/f\\o", "alpha\n", 6);
    make_file("e/.ignore", bracket_rules, sizeof bracket_rules - 1);
    assert(run(2, args) == 0 && err_text[0] == '\0');
    assert(strcmp(out_text, "e/sub/f\\o:alpha\ne/sub/foo:alpha\n") == 0);
}

/* Repeated "**" components match what one does, so a chain of them is
 * matched in linear time, as git and the reference do. A pattern whose
 * retries exceed the matching work limit is an error for the entry, which
 * is then skipped: reported, with status 2, never silently searched. */
static void ignore_work(void)
{
    static const char *const args[] = {"alpha", "@h"};
    static const char collapsing[] = "**/**/**/**/**/**/**/**/zzz\n";
    static const char hostile[] = "**/a/**/a/**/a/**/a/**/a/**/a/**/a/**/zzz\n";
    char rel[128];
    char expected[160];
    char *rules;
    size_t used;
    int depth;
    int i;

    for (depth = 0; depth <= CHAIN_DEPTH; depth++) {
        chain_path(rel, depth, NULL);
        make_dir(rel);
    }
    chain_path(rel, CHAIN_DEPTH, "f");
    make_file(rel, "alpha\n", 6);
    sprintf(expected, "%s:alpha\n", rel);
    make_file("h/.ignore", collapsing, sizeof collapsing - 1);
    assert(run(2, args) == 0 && err_text[0] == '\0');
    assert(strcmp(out_text, expected) == 0);
    make_file("h/.ignore", hostile, sizeof hostile - 1);
    assert(run(2, args) == 2 && out_text[0] == '\0');
    assert(strstr(err_text, "cgrep: ") != NULL && strstr(err_text, "/h/a") != NULL);
    assert(strstr(err_text, "ignore rules exceed the matching work limit") != NULL);
    /* A large file of ordinary rules is not hostile: each rule tried adds
     * to the budget, so the work grows with the file, not against it. */
    rules = (char *)malloc(RULE_COUNT * 16);
    assert(rules != NULL);
    used = 0;
    for (i = 0; i < RULE_COUNT; i++)
        used += sprintf(rules + used, "*.ext%d\n", i);
    make_file("h/.ignore", rules, used);
    free(rules);
    assert(run(2, args) == 0 && err_text[0] == '\0');
    assert(strcmp(out_text, expected) == 0);
    remove_chain();
}

static void depth_guard(void)
{
    static const char *const args[] = {"--no-ignore", "alpha", "@h"};
    static const char *const limited[] = {"--no-ignore", "--max-depth=999", "alpha", "@h"};
    static const char *const at_limit[] = {"--no-ignore", "--max-depth=257", "alpha", "@h"};
    static const char *const zero[] = {"--max-depth", "0", "alpha", "@h/top"};
    char rel[600];
    char path[1200];
    char expected[1600];
    int depth;

    make_dir("h");
    make_file("h/top", "alpha\n", 6);
    for (depth = 1; depth <= 260; depth++) {
        chain_path(rel, depth, NULL);
        make_dir(rel);
    }
    chain_path(rel, 256, "f");
    make_file(rel, "alpha\n", 6);
    chain_path(rel, 257, NULL);
    sprintf(expected, "cgrep: %s/%s: directory nesting exceeds 256 levels\n", root, rel);
    assert(run(3, args) == 2);
    assert(strcmp(err_text, expected) == 0);
    chain_path(rel, 256, "f");
    sprintf(expected, "%s:alpha\nh/top:alpha\n", rel);
    assert(strcmp(out_text, expected) == 0);
    /* A requested depth past the limit is no cutoff: the same report and
     * matches as without one; 257 stops exactly where the limit would. */
    chain_path(rel, 257, NULL);
    sprintf(expected, "cgrep: %s/%s: directory nesting exceeds 256 levels\n", root, rel);
    assert(run(4, limited) == 2 && strcmp(err_text, expected) == 0);
    chain_path(rel, 256, "f");
    sprintf(expected, "%s:alpha\nh/top:alpha\n", rel);
    assert(strcmp(out_text, expected) == 0);
    assert(run(4, at_limit) == 0 && err_text[0] == '\0' && strcmp(out_text, expected) == 0);
    assert(run(4, zero) == 0 && strcmp(out_text, "alpha\n") == 0);
    chain_path(rel, 256, "f");
    sprintf(path, "%s/%s", root, rel);
    assert(remove(path) == 0);
    for (depth = 260; depth >= 0; depth--) {
        chain_path(rel, depth, NULL);
        if (depth == 0) {
            sprintf(path, "%s/h/top", root);
            assert(remove(path) == 0);
        }
        sprintf(path, "%s/%s", root, rel);
        assert(remove(path) == 0);
    }
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
    static const char *const inverted_list[] = {"-lv", "alpha", "@v"};
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
    /* The global excludes file is the test's (empty until ignore_files). */
    make_dir("xdg");
    make_dir("xdg/git");
    sprintf(p, "%s/xdg", root);
    assert(setenv("XDG_CONFIG_HOME", p, 1) == 0);
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
    /* -l -v reads a walked file through: a NUL long after its first
     * selected line still drops it, as for a count. */
    make_dir("v");
    make_file("v/bin", "\nx\nx\0\n", 6);
    make_file("v/text", "beta\n", 5);
    assert(run(3, inverted_list) == 0);
    assert(strcmp(out_text, "v/text\n") == 0 && err_text[0] == '\0');
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
    ignore_files();
    ignore_edges();
    worktrees();
    ignore_globs();
    ignore_work();
    depth_guard();
    remove_tree();
    printf("test_walk: PASS\n");
    return 0;
}
#endif
