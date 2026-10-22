/* Ignore files for directory walks, as the reference applies them by
 * default: .rgignore, .ignore and, inside a git repository, .gitignore,
 * .git/info/exclude and the global excludes file. Included by cgrep.c.
 *
 * Each directory's files are tried from the deepest directory outward; per
 * source the deepest matching rule decides, and the sources rank
 * .rgignore, .ignore, .gitignore, exclude, then global. Within one file the
 * last matching line wins, and a "!" line un-ignores, which also lets a
 * hidden name be searched. git sources stop at the repository root, and
 * apply only when some directory on the way up has a .git entry.
 *
 * Directories above a search root are read too. Their rules see the
 * root's path below them plus the full path below the search root.
 * The global file is matched against the path as walked; core.excludesFile is not read, so
 * only $XDG_CONFIG_HOME/git/ignore or ~/.config/git/ignore is. */

#if !defined(__cerb__) && (defined(__unix__) || defined(__wasi__))
#include <unistd.h>
#define CGREP_GETCWD 1
#endif

enum { IG_CUSTOM, IG_IGNORE, IG_GIT, IG_EXCLUDE, IG_KINDS };

typedef struct {
    const char *glob; /* gitignore glob with escapes, no leading '/' or trailing '/' */
    size_t len;
    int negate;   /* "!": un-ignore */
    int dir_only; /* trailing '/': directories only */
    int anchored; /* a '/' before the end: relative to the file's directory */
} IgnoreRule;

typedef struct {
    IgnoreRule *rules;
    size_t count;
} IgnoreFile;

typedef struct {
    IgnoreFile files[IG_KINDS];
    int has_git;      /* a .git entry: a repository root */
    int above;        /* above the search root */
    const char *path; /* as walked; absolute when above */
    const char *root_below; /* above only: the root's path below this one */
} IgnoreDir;

typedef struct {
    CuArena *arena;
    CuVec dirs;           /* IgnoreDir, outermost first */
    IgnoreFile global;
    const char *cwd;      /* absolute working directory, or NULL */
    const char *root;     /* current search root, as walked */
} IgnoreStack;

/* Matching work one verdict may do: bytes scanned, retries and rules
 * tried. A pattern that exhausts it (each "**" retries at every '/', so
 * several of them cost a product of path depths) fails the verdict instead
 * of running for seconds; the walk reports that. */
#ifndef IGNORE_WORK_LIMIT
#define IGNORE_WORK_LIMIT 1000000UL
#endif
/* Nested "**" retries recurse; deeper than this fails the same way. */
#define IGNORE_DEPTH_LIMIT 256U

typedef struct {
    unsigned long left;
    int failed;
} IgnoreWork;

static const char ignore_overrun[] = "ignore rules exceed the matching work limit";

/* Spends n units of w, or fails it and returns 0. */
static int ignore_charge(IgnoreWork *w, size_t n)
{
    if (w->left < n) {
        w->left = 0;
        w->failed = 1;
        return 0;
    }
    w->left -= (unsigned long)n;
    return 1;
}

/* One component of a gitignore glob against one path component: '*' and
 * '?' never match '/', '[...]' is a class ('!' or '^' negates, ranges,
 * literal backslashes), '\' escapes outside classes, and "**" inside a
 * component acts as '*'. */
static int glob_component(const char *g, size_t gl, const char *s, size_t sl, IgnoreWork *w)
{
    size_t gi;
    size_t si;
    size_t star_g;
    size_t star_s;
    size_t k;
    int negate;
    int hit;
    int have_star;
    unsigned char c;
    unsigned char lo;
    unsigned char hi;

    gi = 0;
    si = 0;
    star_g = 0;
    star_s = 0;
    have_star = 0;
    while (si < sl) {
        if (!ignore_charge(w, 1))
            return 0;
        if (gi < gl && g[gi] == '*') {
            while (gi < gl && g[gi] == '*') {
                if (!ignore_charge(w, 1))
                    return 0;
                gi++;
            }
            have_star = 1;
            star_g = gi;
            star_s = si;
            continue;
        }
        if (gi < gl) {
            c = (unsigned char)s[si];
            if (g[gi] == '?') {
                gi++;
                si++;
                continue;
            }
            if (g[gi] == '[') {
                k = gi + 1;
                negate = k < gl && (g[k] == '!' || g[k] == '^');
                if (negate)
                    k++;
                hit = 0;
                /* A ']' first in the class is literal. */
                if (k < gl && g[k] == ']') {
                    hit = c == ']';
                    k++;
                }
                while (k < gl && g[k] != ']') {
                    if (!ignore_charge(w, 1))
                        return 0;
                    lo = (unsigned char)g[k];
                    hi = lo;
                    if (k + 2 < gl && g[k + 1] == '-' && g[k + 2] != ']') {
                        k += 2;
                        hi = (unsigned char)g[k];
                    }
                    if (lo > hi)
                        return 0;
                    if (c >= lo && c <= hi)
                        hit = 1;
                    k++;
                }
                if (k < gl && hit != negate) {
                    gi = k + 1;
                    si++;
                    continue;
                }
            } else {
                if (g[gi] == '\\' && gi + 1 < gl)
                    gi++;
                if ((unsigned char)g[gi] == c) {
                    gi++;
                    si++;
                    continue;
                }
            }
        }
        if (!have_star)
            return 0;
        gi = star_g;
        si = ++star_s;
    }
    while (gi < gl && g[gi] == '*') {
        if (!ignore_charge(w, 1))
            return 0;
        gi++;
    }
    return gi == gl;
}

/* Length of the component starting at p in a string of length n. */
static size_t component_len(const char *p, size_t n)
{
    const char *slash;

    slash = (const char *)memchr(p, '/', n);
    return slash == NULL ? n : (size_t)(slash - p);
}

/* A glob against a '/'-separated relative path, component by component. A
 * "**" component matches zero or more path components; a trailing one, at
 * least one (it is only reached after a '/', with a component left). depth
 * counts the "**" retries this call is nested in. */
static int glob_path(const char *g, size_t gl, const char *p, size_t pl, unsigned depth, IgnoreWork *w)
{
    size_t gc;
    size_t pc;
    size_t at;

    if (depth > IGNORE_DEPTH_LIMIT) {
        w->failed = 1;
        return 0;
    }
    for (;;) {
        if (!ignore_charge(w, 1))
            return 0;
        if (gl == 0)
            return pl == 0;
        gc = component_len(g, gl);
        if (!ignore_charge(w, gc + 1))
            return 0;
        if (gc == 2 && g[0] == '*' && g[1] == '*') {
            /* Repeated "**" components match what one does (as in git);
             * one keeps the retries below linear instead of exponential. */
            while (gl >= 5 && g[3] == '*' && g[4] == '*' && (gl == 5 || g[5] == '/')) {
                if (!ignore_charge(w, 1))
                    return 0;
                g += 3;
                gl -= 3;
            }
            if (gc == gl)
                return 1;
            /* Zero components, then each later component boundary. */
            if (glob_path(g + 3, gl - 3, p, pl, depth + 1, w))
                return 1;
            for (at = 0; at < pl; at++) {
                if (!ignore_charge(w, 1))
                    return 0;
                if (p[at] == '/' && glob_path(g + 3, gl - 3, p + at + 1, pl - at - 1, depth + 1, w))
                    return 1;
            }
            return 0;
        }
        if (pl == 0)
            return 0;
        pc = component_len(p, pl);
        if (!ignore_charge(w, pc + 1))
            return 0;
        if (!glob_component(g, gc, p, pc, w))
            return 0;
        if (gc == gl)
            return pc == pl;
        if (pc == pl)
            return 0;
        g += gc + 1;
        gl -= gc + 1;
        p += pc + 1;
        pl -= pc + 1;
    }
}

/* 1 if rel is ignored, -1 if un-ignored, 0 if no line matches (or w
 * failed): the last matching line of f decides. An unanchored line is one
 * component (parse_ignore anchors any glob with a '/'), so it can match
 * only the final component of rel, which it sees alone. */
static int file_verdict(const IgnoreFile *f, const char *rel, size_t len, int is_dir, IgnoreWork *w)
{
    const IgnoreRule *r;
    const char *base;
    size_t base_len;
    size_t i;
    int matched;

    base = NULL;
    base_len = 0;
    for (i = f->count; i-- != 0;) {
        if (!ignore_charge(w, 1))
            return 0;
        r = &f->rules[i];
        if (r->dir_only && !is_dir)
            continue;
        if (r->anchored) {
            matched = glob_path(r->glob, r->len, rel, len, 0, w);
        } else {
            if (base == NULL) {
                base = rel + len;
                while (base != rel && base[-1] != '/')
                    base--;
                base_len = (size_t)(rel + len - base);
                if (!ignore_charge(w, base_len + 1))
                    return 0;
            }
            matched = glob_path(r->glob, r->len, base, base_len, 0, w);
        }
        if (w->failed)
            return 0;
        if (matched)
            return r->negate ? -1 : 1;
    }
    return 0;
}

/* Parses one ignore file's text into f, keeping views into text. */
static int parse_ignore(CuArena *a, CuStr text, IgnoreFile *f)
{
    CuVec rules;
    IgnoreRule r;
    const char *line;
    const char *nl;
    size_t len;
    size_t at;
    size_t run;

    CU_VEC_INIT(&rules, a, IgnoreRule);
    for (at = 0; at < text.len; at += len + 1) {
        line = text.ptr + at;
        nl = (const char *)memchr(line, '\n', text.len - at);
        len = nl == NULL ? text.len - at : (size_t)(nl - line);
        r.glob = line;
        r.len = len;
        if (r.len != 0 && r.glob[r.len - 1] == '\r')
            r.len--;
        /* Trailing spaces go unless escaped, which an odd run of
         * backslashes does (as git reads it: "\\ " ends in an escaped
         * backslash and a space that goes). */
        while (r.len != 0 && r.glob[r.len - 1] == ' ') {
            for (run = 0; run + 1 < r.len && r.glob[r.len - 2 - run] == '\\'; run++)
                ;
            if (run % 2 != 0)
                break;
            r.len--;
        }
        if (r.len == 0 || r.glob[0] == '#')
            continue;
        r.negate = r.glob[0] == '!';
        if (r.negate) {
            r.glob++;
            r.len--;
        } else if (r.len >= 2 && r.glob[0] == '\\' && (r.glob[1] == '!' || r.glob[1] == '#')) {
            r.glob++;
            r.len--;
        }
        r.anchored = 0;
        if (r.len != 0 && r.glob[0] == '/') {
            r.anchored = 1;
            r.glob++;
            r.len--;
        }
        r.dir_only = r.len != 0 && r.glob[r.len - 1] == '/';
        if (r.dir_only)
            r.len--;
        if (r.len == 0)
            continue;
        if (memchr(r.glob, '/', r.len) != NULL)
            r.anchored = 1;
        CU_VEC_PUSH(&rules, &r);
    }
    f->count = cu_vec_len(&rules);
    f->rules = CU_VEC_DATA(&rules, IgnoreRule);
    return !cu_vec_failed(&rules);
}

/* Reads dir/name into f; a missing or unreadable file has no rules. */
static int read_ignore(CuArena *a, const char *dir, const char *name, IgnoreFile *f)
{
    CuStr text;
    char *path;

    f->rules = NULL;
    f->count = 0;
    path = join_path(a, dir, cu_str_cstr(name));
    if (path == NULL)
        return 0;
    if (!cu_file_read(a, path, &text))
        return !cu_arena_failed(a);
    return parse_ignore(a, text, f);
}

static const char *const ignore_names[IG_KINDS] = {".rgignore", ".ignore", ".gitignore", ".git/info/exclude"};

/* Pushes dir's ignore files; listed says which names it holds (bits by
 * kind, and 1 << IG_KINDS for .git). above dirs pass root_below. */
static int push_ignore_dir(IgnoreStack *st, const char *dir, unsigned listed, int above, const char *root_below)
{
    IgnoreDir d;
    int k;

    d.has_git = (listed >> IG_KINDS) & 1;
    d.above = above;
    d.path = dir;
    d.root_below = root_below;
    for (k = 0; k < IG_KINDS; k++) {
        d.files[k].rules = NULL;
        d.files[k].count = 0;
        if (k == IG_EXCLUDE ? d.has_git != 0 : ((listed >> k) & 1u) != 0) {
            if (!read_ignore(st->arena, dir, ignore_names[k], &d.files[k]))
                return 0;
        }
    }
    CU_VEC_PUSH(&st->dirs, &d);
    return !cu_vec_failed(&st->dirs);
}

static void pop_ignore_dir(IgnoreStack *st)
{
    IgnoreDir d;

    (void)CU_VEC_POP(&st->dirs, &d);
}

/* Which ignore names (and .git) a directory listing holds. */
static unsigned ignore_listing_bit(CuStr name)
{
    int k;

    for (k = 0; k < IG_EXCLUDE; k++) {
        if (name.len == strlen(ignore_names[k]) && memcmp(name.ptr, ignore_names[k], name.len) == 0)
            return 1u << k;
    }
    if (name.len == 4 && memcmp(name.ptr, ".git", 4) == 0)
        return 1u << IG_KINDS;
    return 0;
}

/* Whether d holds no rules at all: then it cannot decide a verdict. */
static int ignore_dir_empty(const IgnoreDir *d)
{
    int k;

    for (k = 0; k < IG_KINDS; k++) {
        if (d->files[k].count != 0)
            return 0;
    }
    return 1;
}

/* The verdict for path (as walked, ending in its name) under the stack:
 * 1 ignored, -1 un-ignored, 0 neither. When the rules exceed the work
 * limit there is none: 0, with *error set to say why; else *error is NULL. */
static int ignore_verdict(const IgnoreStack *st, const char *path, int is_dir, const char **error)
{
    const IgnoreDir *dirs;
    const IgnoreDir *d;
    IgnoreWork work;
    char relative_path[4096];
    const char *rel;
    const char *below;
    size_t below_len;
    size_t len;
    size_t n;
    size_t i;
    int m[IG_KINDS];
    int any_git;
    int saw_git;
    int k;
    int v;

    *error = NULL;
    work.left = IGNORE_WORK_LIMIT;
    work.failed = 0;
    dirs = CU_VEC_DATA(&st->dirs, IgnoreDir);
    n = cu_vec_len(&st->dirs);
    any_git = 0;
    for (i = 0; i < n; i++)
        any_git |= dirs[i].has_git;
    for (k = 0; k < IG_KINDS; k++)
        m[k] = 0;
    saw_git = 0;
    len = strlen(st->root);
    below = path + len + (len != 0 && st->root[len - 1] != '/');
    below_len = strlen(below);
    for (i = n; i-- != 0;) {
        d = &dirs[i];
        /* Most directories have no rules; only their .git entry counts. */
        if (ignore_dir_empty(d)) {
            saw_git |= d->has_git;
            continue;
        }
        if (d->above) {
            len = strlen(d->root_below);
            if (len + 1 + below_len >= sizeof relative_path)
                continue;
            memcpy(relative_path, d->root_below, len);
            relative_path[len] = '/';
            memcpy(relative_path + len + 1, below, below_len);
            rel = relative_path;
            len += 1 + below_len;
        } else {
            len = strlen(d->path);
            rel = path + len + (len != 0 && d->path[len - 1] != '/');
            len = strlen(rel);
        }
        /* Consulting a directory costs its relative path. */
        if (ignore_charge(&work, len + 1)) {
            for (k = 0; k < IG_KINDS; k++) {
                if (m[k] == 0 && (k < IG_GIT || (any_git && !saw_git)))
                    m[k] = file_verdict(&d->files[k], rel, len, is_dir, &work);
            }
        }
        if (work.failed) {
            *error = ignore_overrun;
            return 0;
        }
        saw_git |= d->has_git;
    }
    for (k = 0; k < IG_KINDS; k++) {
        if (m[k] != 0)
            return m[k];
    }
    if (!any_git || st->global.count == 0)
        return 0;
    /* The global file has no directory of its own: it sees the path as
     * walked, less a leading "./" or "/". */
    rel = path;
    if (rel[0] == '.' && rel[1] == '/')
        rel += 2;
    else if (rel[0] == '/')
        rel++;
    v = file_verdict(&st->global, rel, strlen(rel), is_dir, &work);
    if (work.failed) {
        *error = ignore_overrun;
        return 0;
    }
    return v;
}
