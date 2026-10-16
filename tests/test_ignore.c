#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cu.h"
#include "cgrep.h"
int cgrep_ignore_probe(int argc, char **argv, FILE *in, FILE *out, FILE *err);
#define cgrep_main cgrep_ignore_probe
#include "../src/cgrep.c"
#undef cgrep_main

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
    assert(glob_component(text, n, name, strlen(name)) == expected);
    free(text);
}

static void boundaries(void)
{
    CuArena a;
    IgnoreFile f;
    char *text;

    assert(glob_component("[!a]", 4, "!", 1));
    assert(glob_component("[^a]", 4, "[", 1));
    assert(glob_component("\\*", 2, "*", 1));
    assert(!glob_path("*a", 2, "b", 1));
    assert(glob_path("*a", 2, "ba", 2));
    assert(!glob_path("*a", 2, "", 0));
    assert(!glob_path("sub/", 4, "sub/deep", 8));
    assert(!glob_path("sub/", 4, "sub/x", 5));
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
    assert(file_verdict(&f, "b", 1, 0) == 1);
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
    assert(file_verdict(&f, " ", 1, 0) == 1);
    assert(file_verdict(&f, "\\", 1, 0) == 0);
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
    size_t i;

    cu_arena_init(&a);
    init_stack(&st, &a, "g");
    init_dir(&d, "g", 1);
    assert(parse_ignore(&a, cu_str_cstr("*.log\n"), &d.files[IG_GIT]));
    CU_VEC_PUSH(&st.dirs, &d);
    assert(ignore_verdict(&st, "g/a.log", 0) == 1);
    assert(ignore_verdict(&st, "g/keep", 0) == 0);
    cu_arena_release(&a);
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        cu_arena_init(&a);
        init_stack(&st, &a, cases[i].root_path);
        init_dir(&d, cases[i].root_path, 1);
        CU_VEC_PUSH(&st.dirs, &d);
        assert(parse_ignore(&a, cu_str_cstr(cases[i].pattern), &st.global));
        assert(ignore_verdict(&st, cases[i].path, 0) == 1);
        cu_arena_release(&a);
    }
}

static void ancestor_limit(void)
{
    CuArena a;
    IgnoreStack st;
    IgnoreDir d;
    char path[4100];
    size_t n;
    size_t i;

    cu_arena_init(&a);
    init_stack(&st, &a, "r");
    init_dir(&d, "parent", 0);
    d.above = 1;
    d.root_below = "p";
    assert(parse_ignore(&a, cu_str_cstr("*\n"), &d.files[IG_IGNORE]));
    CU_VEC_PUSH(&st.dirs, &d);
    init_dir(&d, "r", 0);
    CU_VEC_PUSH(&st.dirs, &d);
    for (n = 4093; n <= 4095; n++) {
        memcpy(path, "r/", 2);
        for (i = 0; i < n; i++)
            path[i + 2] = i % 200 == 199 ? '/' : 'x';
        path[n + 2] = '\0';
        assert(ignore_verdict(&st, path, 0) == (n == 4093 ? 1 : 0));
    }
    cu_arena_release(&a);
}

int main(void)
{
    boundaries();
    failures();
    roots();
    ancestor_limit();
    printf("test_ignore: PASS\n");
    return 0;
}
