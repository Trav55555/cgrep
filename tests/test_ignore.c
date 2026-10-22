#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cu.h"
#include "cgrep.h"
int cgrep_ignore_probe(int argc, char **argv, FILE *in, FILE *out, FILE *err);
#define cgrep_main cgrep_ignore_probe
/* A small work limit: what matches within it here is far below the
 * production limit, and what exceeds it shows the matcher doing work it
 * need not. */
#define IGNORE_WORK_LIMIT 16384UL
#include "../src/cgrep.c"
#undef cgrep_main

static IgnoreWork work;

static void fresh_work(void)
{
    work.left = IGNORE_WORK_LIMIT;
    work.failed = 0;
}

/* file_verdict with a fresh budget; work tells what it cost. */
static int verdict(const IgnoreFile *f, const char *rel, int is_dir)
{
    fresh_work();
    return file_verdict(f, rel, strlen(rel), is_dir, &work);
}

static void init_stack(IgnoreStack *st, CuArena *a, const char *root_path)
{
    st->arena = a;
    CU_VEC_INIT(&st->dirs, a, IgnoreDir);
    st->global.rules = NULL;
    st->global.count = 0;
    st->cwd = NULL;
    st->root = root_path;
}

static void init_dir(IgnoreDir *d, const char *path, int git)
{
    int k;

    d->path = path;
    d->has_git = git;
    d->above = 0;
    d->root_below = NULL;
    for (k = 0; k < IG_KINDS; k++) {
        d->files[k].rules = NULL;
        d->files[k].count = 0;
    }
}

static void heap_component(const char *glob, const char *name, int expected)
{
    char *text;
    size_t n;

    n = strlen(glob);
    assert(n != 0);
    text = (char *)malloc(n);
    assert(text != NULL);
    memcpy(text, glob, n);
    fresh_work();
    assert(glob_component(text, n, name, strlen(name), &work) == expected);
    free(text);
}

static void boundaries(void)
{
    CuArena a;
    IgnoreFile f;
    char *text;

    fresh_work();
    assert(glob_component("[!a]", 4, "!", 1, &work));
    assert(glob_component("[^a]", 4, "[", 1, &work));
    assert(glob_component("\\*", 2, "*", 1, &work));
    assert(!glob_path("*a", 2, "b", 1, 0, &work));
    assert(glob_path("*a", 2, "ba", 2, 0, &work));
    assert(!glob_path("*a", 2, "", 0, 0, &work));
    assert(!glob_path("sub/", 4, "sub/deep", 8, 0, &work));
    assert(!glob_path("sub/", 4, "sub/x", 5, 0, &work));
    assert(!work.failed);
    heap_component("foo*", "foox", 1);
    heap_component("[", "x", 0);
    heap_component("[a-", "a", 0);
    heap_component("\\", "\\", 1);
    cu_arena_init(&a);
    text = (char *)malloc(3);
    assert(text != NULL);
    memcpy(text, "a\nb", 3);
    assert(parse_ignore(&a, cu_str_n(text, 3), &f));
    assert(f.count == 2 && f.rules[1].len == 1);
    assert(verdict(&f, "b", 0) == 1);
    cu_arena_release(&a);
    free(text);
    cu_arena_init(&a);
    text = (char *)malloc(1);
    assert(text != NULL);
    text[0] = '\n';
    assert(parse_ignore(&a, cu_str_n(text, 1), &f) && f.count == 0);
    text[0] = '!';
    assert(parse_ignore(&a, cu_str_n(text, 1), &f) && f.count == 0);
    cu_arena_release(&a);
    free(text);
    cu_arena_init(&a);
    assert(parse_ignore(&a, cu_str_cstr(" \n\r\n!\n/\n#comment\n"), &f) && f.count == 0);
    assert(parse_ignore(&a, cu_str_cstr("\\ \n"), &f));
    assert(verdict(&f, " ", 0) == 1);
    assert(verdict(&f, "\\", 0) == 0);
    cu_arena_release(&a);
}

/* n times unit, then tail, in a malloc'd string. */
static char *repeated(const char *unit, size_t n, const char *tail)
{
    char *s;
    size_t ul;
    size_t i;

    ul = strlen(unit);
    s = (char *)malloc(n * ul + strlen(tail) + 1);
    assert(s != NULL);
    for (i = 0; i < n; i++)
        memcpy(s + i * ul, unit, ul);
    strcpy(s + n * ul, tail);
    return s;
}

/* Repeated "**" components match what one does, as in git, in linear
 * time: a chain of them against a deep path stays within the budget.
 * Patterns that retry exponentially, or nest too deep, fail the verdict
 * with a reported error instead of running for seconds. */
static void double_star(void)
{
    static const struct {
        const char *rules;
        int expected;
    } collapsing[] = {{"**/**/**/**/**/**/**/**/zzz\n", 0}, {"**/**/f\n", 1}, {"d/**/**/f\n", 1},
        {"**/**\n", 1}, {"d/**/**\n", 1}, {"**/**/**/d/f\n", 1}, {"**/**/d\n", 0}, {"**/**/**/d\n", 0}};
    CuArena a;
    IgnoreStack st;
    IgnoreDir d;
    IgnoreFile f;
    const char *error;
    char *path;
    char *glob;
    char *text;
    size_t i;

    cu_arena_init(&a);
    path = repeated("d/", 40, "f");
    for (i = 0; i < sizeof collapsing / sizeof collapsing[0]; i++) {
        assert(parse_ignore(&a, cu_str_cstr(collapsing[i].rules), &f));
        assert(verdict(&f, path, 0) == collapsing[i].expected);
        assert(!work.failed);
    }
    assert(parse_ignore(&a, cu_str_cstr("**/**/d\n"), &f));
    assert(verdict(&f, "d", 1) == 1 && verdict(&f, "d/d", 1) == 1 && !work.failed);
    assert(parse_ignore(&a, cu_str_cstr("**/d/**/d/**/d/**/d/**/d/**/d/**/d/**/zzz\n"), &f));
    assert(verdict(&f, path, 0) == 0 && work.failed);
    /* Through the stack: no verdict, and the error says why. */
    init_stack(&st, &a, "r");
    init_dir(&d, "r", 0);
    d.files[IG_IGNORE] = f;
    CU_VEC_PUSH(&st.dirs, &d);
    text = (char *)malloc(strlen(path) + 3);
    assert(text != NULL);
    sprintf(text, "r/%s", path);
    error = "unset";
    assert(ignore_verdict(&st, text, 0, &error) == 0);
    assert(error != NULL && strcmp(error, "ignore rules exceed the matching work limit") == 0);
    free(text);
    free(path);
    /* Each double star before "a" nests a retry: too many of them is the
     * same failure. */
    glob = repeated("**/a/", IGNORE_DEPTH_LIMIT + 2, "zzz\n");
    path = repeated("a/", IGNORE_DEPTH_LIMIT + 2, "f");
    assert(parse_ignore(&a, cu_str_cstr(glob), &f));
    assert(verdict(&f, path, 0) == 0 && work.failed);
    free(glob);
    free(path);
    cu_arena_release(&a);
}

/* A trailing space is escaped only by an odd run of backslashes, as git
 * reads it: "a\\ " (two backslashes) is the glob "a\\", not "a\\ ". */
static void trailing_space(void)
{
    CuArena a;
    IgnoreFile f;

    cu_arena_init(&a);
    assert(parse_ignore(&a, cu_str_cstr("a\\\\ \n"), &f) && f.count == 1 && f.rules[0].len == 3);
    assert(verdict(&f, "a\\", 0) == 1);
    assert(verdict(&f, "a\\ ", 0) == 0);
    assert(parse_ignore(&a, cu_str_cstr("a\\\\  \n"), &f) && f.count == 1 && f.rules[0].len == 3);
    assert(parse_ignore(&a, cu_str_cstr("a\\\\\\ \n"), &f) && f.count == 1 && f.rules[0].len == 5);
    assert(verdict(&f, "a\\ ", 0) == 1);
    assert(verdict(&f, "a\\", 0) == 0);
    assert(parse_ignore(&a, cu_str_cstr("\\\\\n"), &f) && f.count == 1 && f.rules[0].len == 2);
    assert(verdict(&f, "\\", 0) == 1);
    cu_arena_release(&a);
}

/* An unanchored rule is one component (a glob with '/' is anchored), so
 * only the final component can match it: it is matched there once, not
 * retried at every '/' of a long path. */
static void final_component(void)
{
    CuArena a;
    IgnoreFile f;
    char *path;

    cu_arena_init(&a);
    path = repeated("xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx/",
        200, "f");
    assert(parse_ignore(&a, cu_str_cstr("zzz\n"), &f));
    assert(verdict(&f, path, 0) == 0 && !work.failed);
    assert(parse_ignore(&a, cu_str_cstr("x*\n"), &f));
    assert(verdict(&f, path, 0) == 0 && !work.failed);
    assert(parse_ignore(&a, cu_str_cstr("f\n"), &f));
    assert(verdict(&f, path, 0) == 1 && !work.failed);
    assert(parse_ignore(&a, cu_str_cstr("*\n"), &f));
    assert(verdict(&f, path, 0) == 1 && !work.failed);
    free(path);
    /* As before: the final component, whole; "**" alone matches all. */
    assert(parse_ignore(&a, cu_str_cstr("b\n"), &f));
    assert(verdict(&f, "a/b", 0) == 1 && verdict(&f, "a/b/c", 0) == 0 && verdict(&f, "a/bb", 0) == 0);
    assert(parse_ignore(&a, cu_str_cstr("a\n"), &f));
    assert(verdict(&f, "a/b", 0) == 0 && verdict(&f, "b/a", 0) == 1 && verdict(&f, "a", 0) == 1);
    assert(parse_ignore(&a, cu_str_cstr("**\n"), &f));
    assert(verdict(&f, "a/b", 0) == 1 && verdict(&f, "a", 0) == 1);
    assert(parse_ignore(&a, cu_str_cstr("b/\n"), &f));
    assert(verdict(&f, "a/b", 1) == 1 && verdict(&f, "a/b", 0) == 0);
    assert(parse_ignore(&a, cu_str_cstr("a/b\n"), &f));
    assert(verdict(&f, "a/b", 0) == 1 && verdict(&f, "x/a/b", 0) == 0);
    cu_arena_release(&a);
}

/* Directories whose ignore files hold no rules take no part in a verdict,
 * so thousands of them, above or below the root, cost nothing. One with
 * a .git entry still bounds the git sources above it. */
static void empty_frames(void)
{
    CuArena a;
    IgnoreStack st;
    IgnoreDir d;
    const char *error;
    size_t i;

    cu_arena_init(&a);
    init_stack(&st, &a, "r");
    init_dir(&d, "/home", 0);
    d.above = 1;
    d.root_below = "r";
    for (i = 0; i < 20000; i++)
        CU_VEC_PUSH(&st.dirs, &d);
    init_dir(&d, "r", 1);
    assert(parse_ignore(&a, cu_str_cstr("*.log\n!keep.log\n"), &d.files[IG_GIT]));
    CU_VEC_PUSH(&st.dirs, &d);
    init_dir(&d, "r/s", 0);
    for (i = 0; i < 20000; i++)
        CU_VEC_PUSH(&st.dirs, &d);
    assert(!cu_vec_failed(&st.dirs));
    assert(ignore_verdict(&st, "r/s/x.log", 0, &error) == 1 && error == NULL);
    assert(ignore_verdict(&st, "r/s/keep.log", 0, &error) == -1 && error == NULL);
    assert(ignore_verdict(&st, "r/s/x.txt", 0, &error) == 0 && error == NULL);
    cu_arena_release(&a);
    cu_arena_init(&a);
    init_stack(&st, &a, "r");
    init_dir(&d, "/", 0);
    d.above = 1;
    d.root_below = "home/r";
    assert(parse_ignore(&a, cu_str_cstr("*.log\n"), &d.files[IG_GIT]));
    CU_VEC_PUSH(&st.dirs, &d);
    init_dir(&d, "/home", 1);
    d.above = 1;
    d.root_below = "r";
    CU_VEC_PUSH(&st.dirs, &d);
    init_dir(&d, "r", 0);
    CU_VEC_PUSH(&st.dirs, &d);
    assert(ignore_verdict(&st, "r/x.log", 0, &error) == 0 && error == NULL);
    cu_arena_release(&a);
}

static void failures(void)
{
    CuArena a;
    IgnoreFile f;
    IgnoreStack st;

    cu_arena_init(&a);
    cu_arena_fail_at(&a, 0);
    assert(!read_ignore(&a, ".", ".ignore", &f));
    assert(cu_arena_failed(&a) && f.count == 0 && f.rules == NULL);
    cu_arena_release(&a);
    cu_arena_init(&a);
    init_stack(&st, &a, ".");
    cu_arena_fail_at(&a, 0);
    assert(!push_ignore_dir(&st, ".", 1u << IG_IGNORE, 0, NULL));
    assert(cu_arena_failed(&a) && cu_vec_len(&st.dirs) == 0);
    cu_arena_release(&a);
    cu_arena_init(&a);
    cu_arena_fail_at(&a, 0);
    assert(!parse_ignore(&a, cu_str_cstr("x\n"), &f));
    assert(cu_arena_failed(&a) && f.count == 0);
    cu_arena_release(&a);
}

static void roots(void)
{
    static const struct {
        const char *root_path;
        const char *path;
        const char *pattern;
    } cases[] = {{".", "./x", "/x\n"}, {"/", "/x", "/x\n"}, {"g", "g/x", "/g/x\n"},
        {"//", "//x", "**/x\n"}, {"//", "//x", "x\n"}};
    CuArena a;
    IgnoreStack st;
    IgnoreDir d;
    const char *error;
    size_t i;

    cu_arena_init(&a);
    init_stack(&st, &a, "g");
    init_dir(&d, "g", 1);
    assert(parse_ignore(&a, cu_str_cstr("*.log\n"), &d.files[IG_GIT]));
    CU_VEC_PUSH(&st.dirs, &d);
    assert(ignore_verdict(&st, "g/a.log", 0, &error) == 1 && error == NULL);
    assert(ignore_verdict(&st, "g/keep", 0, &error) == 0 && error == NULL);
    cu_arena_release(&a);
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        cu_arena_init(&a);
        init_stack(&st, &a, cases[i].root_path);
        init_dir(&d, cases[i].root_path, 1);
        CU_VEC_PUSH(&st.dirs, &d);
        assert(parse_ignore(&a, cu_str_cstr(cases[i].pattern), &st.global));
        assert(ignore_verdict(&st, cases[i].path, 0, &error) == 1 && error == NULL);
        cu_arena_release(&a);
    }
}

/* "r/" then n bytes of 'x' with a '/' every 200th, in a malloc'd string. */
static char *long_path(size_t n)
{
    char *path;
    size_t i;

    path = (char *)malloc(n + 3);
    assert(path != NULL);
    memcpy(path, "r/", 2);
    for (i = 0; i < n; i++)
        path[i + 2] = i % 200 == 199 ? '/' : 'x';
    path[n + 2] = '\0';
    return path;
}

/* An ancestor's rules see the path below it however long it is: an
 * unanchored rule as well as an anchored one that needs the whole path,
 * which is joined in the arena for the verdict. Without memory for it
 * there is no verdict, and the error says so. */
static void long_paths(void)
{
    CuArena a;
    IgnoreStack st;
    IgnoreDir *above;
    IgnoreDir d;
    const char *error;
    char *path;
    size_t n;

    cu_arena_init_with(&a, 4096, 0);
    init_stack(&st, &a, "r");
    init_dir(&d, "parent", 0);
    d.above = 1;
    d.root_below = "p";
    assert(parse_ignore(&a, cu_str_cstr("*\n"), &d.files[IG_IGNORE]));
    CU_VEC_PUSH(&st.dirs, &d);
    init_dir(&d, "r", 0);
    CU_VEC_PUSH(&st.dirs, &d);
    for (n = 4093; n <= 4095; n++) {
        path = long_path(n);
        assert(ignore_verdict(&st, path, 0, &error) == 1 && error == NULL);
        free(path);
    }
    path = long_path(12099);
    assert(ignore_verdict(&st, path, 0, &error) == 1 && error == NULL);
    above = CU_VEC_AT(&st.dirs, IgnoreDir, 0);
    assert(parse_ignore(&a, cu_str_cstr("p/**\n"), &above->files[IG_IGNORE]));
    assert(ignore_verdict(&st, path, 0, &error) == 1 && error == NULL);
    assert(parse_ignore(&a, cu_str_cstr("q/**\n"), &above->files[IG_IGNORE]));
    assert(ignore_verdict(&st, path, 0, &error) == 0 && error == NULL);
    cu_arena_fail_at(&a, a.attempts);
    error = NULL;
    assert(ignore_verdict(&st, path, 0, &error) == 0);
    assert(error != NULL && strcmp(error, "out of memory") == 0);
    free(path);
    cu_arena_release(&a);
}

int main(void)
{
    boundaries();
    double_star();
    trailing_space();
    final_component();
    empty_frames();
    failures();
    roots();
    long_paths();
    printf("test_ignore: PASS\n");
    return 0;
}
