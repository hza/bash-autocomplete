/*
 * autocomplete - tiny carapace-like completion engine for bash 3.2 (macOS).
 *
 *   eval "$(autocomplete bash)"          # register completions for all specs
 *   autocomplete _complete "$COMP_LINE_UP_TO_POINT" "$COMP_WORDBREAKS"
 *
 * Commands are described by YAML specs, <command>.yaml or <command>.yml,
 * looked up in:
 *   $AUTOCOMPLETE_SPECS (colon list), ~/.config/autocomplete,
 *   <binary dir>/specs, <binary dir>/../share/autocomplete
 * `autocomplete from man CMD` writes a spec for CMD from its man page.
 *
 * Output of _complete: one candidate per line, already shell-escaped and
 * already trimmed to bash's notion of the current word (COMP_WORDBREAKS).
 * Candidates that are "finished" end with a space; bash 3.2 has no compopt,
 * so the function is registered with -o nospace and we add spaces ourselves.
 */
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "manparse.h"
#include "yaml.h"

/* ------------------------------------------------------------ spec dirs */

#define MAXDIRS 16

static char *g_dirs[MAXDIRS];
static int g_ndirs;

static void add_dir(const char *d) {
    if (g_ndirs < MAXDIRS && d && *d) g_dirs[g_ndirs++] = strdup(d);
}

static void init_dirs(void) {
    char buf[PATH_MAX], exe[PATH_MAX], *env = getenv("AUTOCOMPLETE_SPECS"), *slash;
    uint32_t size = sizeof buf;

    if (env) {
        char *copy = strdup(env), *tok;
        for (tok = strtok(copy, ":"); tok; tok = strtok(NULL, ":")) add_dir(tok);
        free(copy);
    }
    if (getenv("HOME")) {
        snprintf(buf, sizeof buf, "%s/.config/autocomplete", getenv("HOME"));
        add_dir(buf);
    }
#ifdef __APPLE__
    if (_NSGetExecutablePath(buf, &size) != 0 || !realpath(buf, exe)) return;
#else
    (void)size;
    if (readlink("/proc/self/exe", exe, sizeof exe - 1) <= 0) return;
#endif
    if (!(slash = strrchr(exe, '/'))) return;
    *slash = 0;
    snprintf(buf, sizeof buf, "%s/specs", exe);
    add_dir(buf);
    snprintf(buf, sizeof buf, "%s/../share/autocomplete", exe);
    add_dir(buf);
}

static const char *g_exts[] = {".yaml", ".yml", 0};

/* Path of the YAML spec for `name` (first dir/ext that exists), or 0. */
static int find_spec(const char *name, char *path, size_t size) {
    int i, e;
    if (strchr(name, '/') || name[0] == '.') return 0;
    for (i = 0; i < g_ndirs; i++)
        for (e = 0; g_exts[e]; e++) {
            snprintf(path, size, "%s/%s%s", g_dirs[i], name, g_exts[e]);
            if (access(path, R_OK) == 0) return 1;
        }
    return 0;
}

/* mkdir -p */
static void mkdirs(char *dir) {
    char *s;
    for (s = dir + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(dir, 0755); *s = '/'; }
    mkdir(dir, 0755);
}

/* Compiled specs live in $XDG_CACHE_HOME/autocomplete or ~/.cache/autocomplete
 * as <name>.cache; creates the directory when `create` is set. */
static int cache_path(const char *name, char *path, size_t size, int create) {
    const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    char dir[PATH_MAX];
    if (getenv("AUTOCOMPLETE_NOCACHE")) return 0;
    if (xdg && *xdg) snprintf(dir, sizeof dir, "%s/autocomplete", xdg);
    else if (home) snprintf(dir, sizeof dir, "%s/.cache/autocomplete", home);
    else return 0;
    if (create) mkdirs(dir);
    snprintf(path, size, "%s/%s.cache", dir, name);
    return 1;
}

static int save_cache(const char *name, const ynode *n, const char *src, char *out, size_t size) {
    return cache_path(name, out, size, 1) && yaml_save_bin(n, out, src);
}

#ifdef __APPLE__
#define MTIME(st) (st).st_mtimespec
#else
#define MTIME(st) (st).st_mtim
#endif

/* Only YAML bigger than this is compiled to a cache; smaller specs parse
 * about as fast as the cache loads. */
#define CACHE_MIN (100 * 1024)

/* Load a spec; a big one from its compiled cache when that is newer than the YAML. */
static ynode *load_spec(const char *name) {
    char path[PATH_MAX], bin[PATH_MAX], err[256];
    struct stat ys, bs;
    int big;
    ynode *n;
    if (!find_spec(name, path, sizeof path)) return NULL;
    big = stat(path, &ys) == 0 && ys.st_size > CACHE_MIN;
    if (big && cache_path(name, bin, sizeof bin, 0) && stat(bin, &bs) == 0 &&
        (MTIME(bs).tv_sec > MTIME(ys).tv_sec ||
         (MTIME(bs).tv_sec == MTIME(ys).tv_sec && MTIME(bs).tv_nsec > MTIME(ys).tv_nsec)) &&
        (n = yaml_load_bin(bin, path)))
        return n;
    n = yaml_load(path, err, sizeof err);
    if (!n && getenv("AUTOCOMPLETE_DEBUG")) fprintf(stderr, "autocomplete: %s: %s\n", path, err);
    if (n && big && !save_cache(name, n, path, bin, sizeof bin) && getenv("AUTOCOMPLETE_DEBUG"))
        fprintf(stderr, "autocomplete: cannot write cache for %s\n", name);
    return n;
}

/* ------------------------------------------------------------- output */

static const char *g_cur;   /* current word, unescaped (for filtering) */
static const char *g_raw;   /* current word as typed (for wordbreak trimming) */
static const char *g_wbreaks;

/* flag values seen on the line, e.g. make -C dir */
#define MAXSEEN 64
static const char *g_seen_flag[MAXSEEN], *g_seen_val[MAXSEEN];
static int g_nseen;

static const char *seen(const char *a, const char *b, const char *c) {
    int i;
    for (i = g_nseen - 1; i >= 0; i--) {
        const char *f = g_seen_flag[i];
        if (!strcmp(f, a) || (b && !strcmp(f, b)) || (c && !strcmp(f, c))) return g_seen_val[i];
    }
    return NULL;
}

/* Candidates are collected and printed by flush() once completion is done,
 * so the output can depend on how many matched. */
#define MAXCANDS 4096
typedef struct {
    char *text;       /* escaped, trimmed to bash's current word */
    int space;        /* finished word: append a space */
    const char *desc; /* optional description (spec "description") */
    const char *kind; /* source, e.g. "target"; tagged when kinds mix */
    int show;         /* flag "show": SHOW_DEFAULT, SHOW_ALWAYS (never folded), SHOW_COLLAPSED (only in 📌) */
    const char *group; /* key in "groups" ("Up"): collapsed together */
    const char *gdesc; /* that group's description */
} cand;
static cand g_cands[MAXCANDS];
static int g_ncands;
static const char *g_kind; /* kind given to candidates emitted now */
enum { SHOW_DEFAULT, SHOW_ALWAYS, SHOW_COLLAPSED };
static int g_show;         /* "show" given to candidates emitted now */
static int g_noesc;        /* display-only candidates: no shell escaping */
static const char *g_group, *g_gdesc; /* group (and its description) given to candidates emitted now */

/* Escape for bash and queue, trimmed to bash's current word. Duplicates
 * (union values can overlap) are dropped, keeping the first description. */
static void emit_desc(const char *cand, int space, const char *desc) {
    char esc[4096];
    size_t n = 0;
    const char *p;
    if (strncmp(cand, g_cur, strlen(g_cur)) != 0) return;
    for (p = cand; *p && n < sizeof esc - 3; p++) {
        if (!g_noesc && strchr(" \t\\'\"()<>&;|*?[]$`!#{}~", *p)) esc[n++] = '\\';
        esc[n++] = *p;
    }
    esc[n] = 0;

    /* bash splits words at COMP_WORDBREAKS; the reply must replace only the
     * part after the last break char of the raw word. */
    p = esc;
    {
        const char *r = g_raw, *last = NULL;
        for (; *r; r++)
            if (strchr(g_wbreaks, *r)) last = r;
        if (last) {
            size_t k = (size_t)(last - g_raw) + 1;
            if (strncmp(esc, g_raw, k) == 0) p = esc + k;
        }
    }
    {
        int i;
        for (i = 0; i < g_ncands; i++)
            if (!strcmp(g_cands[i].text, p)) {
                if (!g_cands[i].desc && desc) g_cands[i].desc = strdup(desc);
                return;
            }
    }
    if (g_ncands < MAXCANDS) {
        g_cands[g_ncands].text = strdup(p);
        g_cands[g_ncands].space = space;
        g_cands[g_ncands].kind = g_kind;
        g_cands[g_ncands].show = g_show;
        g_cands[g_ncands].group = g_group;
        g_cands[g_ncands].gdesc = g_gdesc;
        g_cands[g_ncands++].desc = desc ? strdup(desc) : NULL;
    }
}

static void emit(const char *cand, int space) { emit_desc(cand, space, NULL); }

static int term_width(void) {
    struct winsize ws;
    int fd = open("/dev/tty", O_RDONLY), w = 0;
    if (fd >= 0) {
        if (ioctl(fd, TIOCGWINSZ, &ws) == 0) w = ws.ws_col;
        close(fd);
    }
    if (w <= 0 && getenv("COLUMNS")) w = atoi(getenv("COLUMNS"));
    return w > 20 ? w : 80;
}

/* Print queued candidates. With several matches and any description, each
 * candidate becomes "name  -- description", padded so bash lists one per
 * line. When candidates of different kinds mix (make targets and variables),
 * the description starts with a "[kind]" tag, since bash sorts the list and
 * real section headers are impossible. bash only inserts the common prefix of ambiguous matches, and names
 * differ before the padding starts, so descriptions never reach the command
 * line. A single match is always printed bare so it can be inserted. */
/* On an empty word, make variables are folded into one display-only
 * "🔧 vars" entry (description: their names) so the list shows targets
 * first; typing a letter lists matching ones. It is never inserted: other
 * candidates remain and share no prefix with it. Kept unfolded when there
 * are no targets to show. */
static int is_var(const cand *c) { return c->kind && !strcmp(c->kind, "var"); }

static void collapse_vars(void) {
    char names[1024] = "";
    int i, j, hidden = 0;
    size_t have = 0;
    if (*g_cur) return;
    for (i = 0; i < g_ncands; i++)
        if (is_var(&g_cands[i])) {
            const char *t = g_cands[i].text;
            hidden++;
            if (have < sizeof names - 1)
                have += (size_t)snprintf(names + have, sizeof names - have, "%s%.*s", have ? " " : "",
                                         (int)strcspn(t, "="), t);
        }
    if (!hidden || hidden == g_ncands) return;
    for (i = j = 0; i < g_ncands; i++)
        if (!is_var(&g_cands[i])) g_cands[j++] = g_cands[i];
    g_ncands = j;
    g_kind = "target"; /* sits among targets: no [kind] tags */
    /* U+00A0 no-break space: a plain one would be shown escaped ("\ ") */
    emit_desc("🔧 variables", 0, strdup(names));
}

/* Flags are folded into display-only "<word>📌" entries:
 * - "show: collapsed" flags always: short ones into "<word>📌<letters>",
 *   one entry per kind of letter ("-📌 0123" for digits, "-📌 MP" for
 *   letters, then other symbols), description "(1) desc (2) desc"; long
 *   ones into "<word>📌" ("N more: names" when more than 5);
 * - on a bare "-", the long "--x" flags (unless "show: always") when there
 *   are 15 or more, into "-📌 --" ("N long options: type -- to list"), so
 *   the short flags stand out. Collapsed long flags go there too.
 * Nothing is folded when nothing else would be listed. "📌" sorts after
 * ASCII, so they are listed last. Never inserted: other candidates remain
 * and share only the typed word with them. */
static int is_long_fold(const cand *c, int longs) {
    return longs && c->show != SHOW_ALWAYS && !strncmp(c->text, "--", 2);
}

static int cmp_cand(const void *a, const void *b) {
    return strcmp(((const cand *)a)->text, ((const cand *)b)->text);
}

/* 0 digit, 1 upper, 2 lower, 3 other short flag, 4 long flag */
static int coll_class(const cand *c) {
    unsigned char ch = (unsigned char)c->text[1];
    if (ch == '-') return 4;
    return isdigit(ch) ? 0 : isupper(ch) ? 1 : islower(ch) ? 2 : 3;
}

static int dwidth(const char *s);

/* Letter of short flag candidate c; its text is shell-escaped ("-\\#"). */
static char flag_char(const cand *c) { return c->text[c->text[1] == '\\' ? 2 : 1]; }

/* Is c in the entry for group grp (NULL: ungrouped flags of class cl)? */
static int coll_member(const cand *c, const char *grp, int cl) {
    if (grp) return c->group && !strcmp(c->group, grp);
    return !c->group && coll_class(c) == cl;
}

/* Emit "<word>📌 <letters>" for the short collapsed flags of group grp
 * (or, ungrouped, of class cl), starting a new entry whenever the
 * description would pass `room`. */
static void emit_short_coll(const cand *coll, int k, const char *grp, int cl, int room) {
    char desc[1024], text[256], item[512];
    size_t have = 0, nt = 0;
    int i;
    if (grp) { /* letters and descriptions in the order of the key */
        const char *p;
        const char *gd = NULL;
        nt = (size_t)snprintf(text, sizeof text, "%s📌 ", g_cur);
        desc[0] = 0;
        for (p = grp; *p; p++)
            for (i = 0; i < k; i++) {
                if (!coll_member(&coll[i], grp, cl) || flag_char(&coll[i]) != *p) continue;
                if (!gd && (gd = coll[i].gdesc)) have = (size_t)snprintf(desc, sizeof desc, "%s:", gd);
                if (nt < sizeof text - 1) text[nt++] = *p, text[nt] = 0;
                if (have < sizeof desc - 1)
                    have += (size_t)snprintf(desc + have, sizeof desc - have, "%s(%c) %s", have ? " " : "", *p,
                                             coll[i].desc ? coll[i].desc : "");
            }
        if (have) emit_desc(text, 0, desc);
        return;
    }
    for (i = 0; i <= k; i++) {
        size_t il = 0;
        if (i < k) {
            if (!coll_member(&coll[i], grp, cl)) continue;
            il = (size_t)snprintf(item, sizeof item, "(%c) %s", flag_char(&coll[i]), coll[i].desc ? coll[i].desc : "");
        }
        if (have && (i == k || (int)(have + 1 + il) > room)) {
            emit_desc(text, 0, desc);
            have = 0;
        }
        if (i == k) break;
        if (!have) {
            nt = (size_t)snprintf(text, sizeof text, "%s📌\u00a0", g_cur);
            desc[0] = 0;
        }
        if (nt < sizeof text - 1) text[nt++] = flag_char(&coll[i]), text[nt] = 0;
        if (have < sizeof desc - 1)
            have += (size_t)snprintf(desc + have, sizeof desc - have, "%s%s", have ? " " : "", item);
    }
}

/* "show: collapsed" hides short flags only; long ones are listed (and
 * grouped by letter) as usual, except in the "-📌" long-options fold. */
static int is_coll(const cand *c) { return c->show == SHOW_COLLAPSED && c->text[1] != '-'; }

static void collapse_flags(void) {
    static cand coll[MAXCANDS];
    char desc[1024], text[256];
    const char *kind = NULL;
    int i, j, k = 0, cl, nlong = 0, ncoll = 0, longs = 0;
    if (g_cur[0] != '-') return;
    if (!strcmp(g_cur, "-")) {
        for (i = 0; i < g_ncands; i++)
            if (is_long_fold(&g_cands[i], 1)) longs++;
        longs = longs >= 15;
    }
    for (i = 0; i < g_ncands; i++) {
        if (is_long_fold(&g_cands[i], longs)) nlong++;
        else if (is_coll(&g_cands[i])) ncoll++;
        else continue;
        kind = g_cands[i].kind;
    }
    if (!nlong && !ncoll) return;
    if (nlong + ncoll == g_ncands) return;
    for (i = j = 0; i < g_ncands; i++) {
        if (is_long_fold(&g_cands[i], longs)) continue;
        if (is_coll(&g_cands[i])) coll[k++] = g_cands[i];
        else g_cands[j++] = g_cands[i];
    }
    g_ncands = j;
    qsort(coll, (size_t)k, sizeof *coll, cmp_cand); /* "-📌 123" */
    g_kind = kind;
    g_show = SHOW_ALWAYS;
    g_noesc = 1; /* never inserted: show "-📌 Z#N", not "Z\\#N" */
    { /* name column: widest remaining name (see flush) */
        int w = 12;
        for (i = 0; i < g_ncands; i++)
            if (dwidth(g_cands[i].text) > w) w = dwidth(g_cands[i].text);
        for (cl = 0; cl < 4; cl++) emit_short_coll(coll, k, NULL, cl, term_width() - w - 8);
    }
    for (i = 0; i < k; i++) { /* groups, each once */
        const char *g = coll[i].group;
        if (!g) continue;
        for (j = 0; j < i && !(coll[j].group && !strcmp(coll[j].group, g)); j++) {}
        if (j < i) continue;
        emit_short_coll(coll, k, g, 0, 0);
    }
    if (nlong) {
        snprintf(desc, sizeof desc, "%d long options: type -- to list", nlong);
        snprintf(text, sizeof text, "%s📌 --", g_cur);
        emit_desc(text, 0, desc);
    }
    g_show = SHOW_DEFAULT;
    g_noesc = 0;
}

/* A short flag listed next to a long one with the same description is
 * shown only as a hint on the long one: "--data  -- HTTP POST data (🔥 -d)".
 * Each long flag takes at most one short. */
static void merge_pairs(void) {
    static char paired[MAXCANDS];
    int i, j, k;
    if (g_cur[0] != '-') return;
    memset(paired, 0, (size_t)g_ncands);
    for (i = k = 0; i < g_ncands; i++) {
        cand *c = &g_cands[i];
        int hit = -1;
        if (c->text[1] && c->text[1] != '-' && c->desc && !strstr(c->text, "📌"))
            for (j = 0; j < g_ncands; j++)
                if (!paired[j] && g_cands[j].text && !strncmp(g_cands[j].text, "--", 2) && g_cands[j].desc &&
                    !strcmp(g_cands[j].desc, c->desc)) {
                    hit = j;
                    break;
                }
        if (hit < 0) continue;
        {
            char *d = malloc(strlen(c->desc) + strlen(c->text) + 8);
            if (!d) continue;
            sprintf(d, "%s (🔥 %s)", c->desc, c->text);
            paired[hit] = 1;
            g_cands[hit].desc = d;
            c->text = NULL; /* dropped below */
        }
    }
    for (i = k = 0; i < g_ncands; i++)
        if (g_cands[i].text) g_cands[k++] = g_cands[i];
    g_ncands = k;
}

/* A flag list taller than the terminal is grouped by the next letter after
 * the typed word: "--content-disposition" and "--content-on-error" fall
 * into one "--c" entry (no space, description: the member names, those
 * sharing a first word shown once as "word-🧩"), so typing the letter lists
 * that group. Then the smallest groups are expanded back while the list
 * still fits, so a group of two only stays folded when space is short. */
static int term_rows(void) {
    struct winsize ws;
    int fd = open("/dev/tty", O_RDONLY), h = 0;
    if (fd >= 0) {
        if (ioctl(fd, TIOCGWINSZ, &ws) == 0) h = ws.ws_row;
        close(fd);
    }
    if (h <= 0 && getenv("LINES")) h = atoi(getenv("LINES"));
    return h > 5 ? h : 24;
}

static size_t group_len(const char *t, size_t n) {
    const char *p = t + n;
    while (*p == '-') p++;
    return (size_t)(p - t) + (*p != 0);
}

static char *group_names(const int *gid, int g) {
    char names[1024] = "";
    size_t have = 0;
    int a, b;
    for (a = 0; a < g_ncands; a++) {
        const char *ma = g_cands[a].text, *mb;
        size_t w;
        int same = 0, first = 1;
        if (gid[a] != g) continue;
        while (*ma == '-') ma++;
        w = strcspn(ma, "-");
        /* names sharing a first word: "sasl-authzid sasl-ir" -> "sasl-🧩" */
        if (ma[w])
            for (b = 0; b < g_ncands; b++) {
                if (gid[b] != g) continue;
                for (mb = g_cands[b].text; *mb == '-'; mb++) {}
                if (!strncmp(mb, ma, w + 1)) {
                    same++;
                    if (b < a) first = 0;
                }
            }
        if (same >= 2 && !first) continue;
        if (have < sizeof names - 1)
            have += (size_t)snprintf(names + have, sizeof names - have, "%s%.*s%s", have ? " " : "",
                                     same >= 2 ? (int)w + 1 : (int)strlen(ma), ma, same >= 2 ? "🧩" : "");
    }
    return strdup(names);
}

static void group_flags(void) {
    static cand out[MAXCANDS];
    static int gid[MAXCANDS], head[MAXCANDS], size[MAXCANDS];
    size_t n = strlen(g_cur), len;
    int i, j, k = 0, ng = 0, lines, limit = term_rows() - 2;
    if (g_cur[0] != '-' || g_ncands <= limit) return;
    for (i = 0; i < g_ncands; i++) {
        gid[i] = -1;
        if (strncmp(g_cands[i].text, g_cur, n) || g_cands[i].show == SHOW_ALWAYS) continue;
        len = group_len(g_cands[i].text, n);
        for (j = 0; j < ng; j++)
            if (group_len(g_cands[head[j]].text, n) == len && !strncmp(g_cands[head[j]].text, g_cands[i].text, len))
                break;
        if (j == ng) { head[ng] = i; size[ng++] = 0; }
        gid[i] = j;
        size[j]++;
    }
    lines = g_ncands;
    for (j = 0; j < ng; j++)
        if (size[j] > 1) lines -= size[j] - 1;
    for (;;) { /* expand the smallest folded group that still fits */
        int best = -1;
        for (j = 0; j < ng; j++)
            if (size[j] > 1 && lines + size[j] - 1 <= limit && (best < 0 || size[j] < size[best])) best = j;
        if (best < 0) break;
        lines += size[best] - 1;
        size[best] = 1;
    }
    for (i = 0; i < g_ncands; i++) {
        j = gid[i];
        if (j < 0 || size[j] == 1) { out[k++] = g_cands[i]; continue; }
        if (head[j] != i) continue;
        out[k] = g_cands[i];
        { /* the entry is the members' common prefix: "--no-" for "--no-x --no-y" */
            size_t lcp = strlen(g_cands[i].text);
            int b;
            for (b = i + 1; b < g_ncands; b++)
                if (gid[b] == j) {
                    size_t c = 0;
                    while (c < lcp && g_cands[b].text[c] == g_cands[i].text[c]) c++;
                    lcp = c;
                }
            out[k].text = strndup(g_cands[i].text, lcp);
        }
        out[k].space = 0;
        out[k++].desc = group_names(gid, j);
    }
    memcpy(g_cands, out, (size_t)k * sizeof *out);
    g_ncands = k;
}

/* Terminal columns of a UTF-8 string: continuation bytes take none and
 * 4-byte sequences (emoji) take two. */
static int dwidth(const char *s) {
    int w = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if ((c & 0xC0) != 0x80) w += c >= 0xF0 ? 2 : 1;
    }
    return w;
}

/* On an empty word, the spec's "examples" ({key, command, description?})
 * are listed as display-only "📌 key" entries (spaces in key become
 * U+00A0), described by "command   📚 description". They share
 * "📌 ", so when nothing else is listed a "… name" entry (first byte
 * differs) is added to keep bash from inserting that prefix. */
static const ynode *g_examples;
static const char *g_examples_name, *g_examples_title; /* "… name  -- name: spec description" */
static const char *spec_desc(const ynode *spec);

static void add_examples(void) {
    const ynode *e;
    int n = 0, others = g_ncands;
    char name[256], desc[1024];
    if (*g_cur || !g_examples || g_examples->type != Y_ARR) return;
    g_kind = g_ncands ? g_cands[0].kind : NULL; /* no [kind] tags */
    g_noesc = 1;
    for (e = g_examples->child; e; e = e->next) {
        const ynode *cmd = yaml_get(e, "command");
        const ynode *key = yaml_get(e, "key");
        const char *d = spec_desc(e), *k;
        size_t len = strlen("📌\xc2\xa0");
        if (!cmd || cmd->type != Y_STR || !key || key->type != Y_STR) continue;
        memcpy(name, "📌\xc2\xa0", len); /* U+00A0: see collapse_vars */
        for (k = key->str; *k && len < sizeof name - 3; k++) {
            if (*k == ' ') { name[len++] = '\xc2'; name[len++] = '\xa0'; }
            else name[len++] = *k;
        }
        name[len] = 0;
        n++;
        if (d) snprintf(desc, sizeof desc, "%s   📚 %s", cmd->str, d);
        emit_desc(name, 0, d ? desc : cmd->str);
    }
    if (n && !others) {
        snprintf(desc, sizeof desc, "%s: %s", g_examples_name, g_examples_title ? g_examples_title : "type an argument");
        snprintf(name, sizeof name, "……\xc2\xa0%s", g_examples_name); /* U+00A0 */
        emit_desc(name, 0, desc);
    }
    g_noesc = 0;
}

static void flush(void) {
    int i, any = 0, width = 0, kwidth = 0, mixed = 0, cols, line;
    collapse_vars();
    add_examples();
    collapse_flags();
    merge_pairs();
    group_flags();
    for (i = 0; i < g_ncands; i++) {
        int len = dwidth(g_cands[i].text);
        const char *k = g_cands[i].kind;
        if (g_cands[i].desc) any = 1;
        if (len > width) width = len;
        if (k && (int)strlen(k) + 2 > kwidth) kwidth = (int)strlen(k) + 2;
        if (i && (k ? !g_cands[0].kind || strcmp(k, g_cands[0].kind) : !!g_cands[0].kind)) mixed = 1;
    }
    if (mixed) any = 1;
    if (g_ncands < 2 || !any) {
        for (i = 0; i < g_ncands; i++)
            printf("%s%s\n", g_cands[i].text, g_cands[i].space ? " " : "");
        return;
    }
    cols = term_width();
    line = cols / 2 + 1; /* wider than half the screen: one per line */
    for (i = 0; i < g_ncands; i++) {
        char buf[4096];
        char tag[64] = "";
        int n;
        if (mixed && g_cands[i].kind) snprintf(tag, sizeof tag, "[%s]", g_cands[i].kind);
        if (mixed)
            n = snprintf(buf, sizeof buf, "%-*s  -- %-*s %s",
                         width + (int)strlen(g_cands[i].text) - dwidth(g_cands[i].text), g_cands[i].text, kwidth, tag,
                         g_cands[i].desc ? g_cands[i].desc : "");
        else if (g_cands[i].desc)
            n = snprintf(buf, sizeof buf, "%-*s  -- %s",
                         width + (int)strlen(g_cands[i].text) - dwidth(g_cands[i].text), g_cands[i].text, g_cands[i].desc);
        else
            n = snprintf(buf, sizeof buf, "%s", g_cands[i].text);
        if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
        buf[n] = 0;
        while (dwidth(buf) > cols - 2) { /* avoid wrapping; don't split a UTF-8 sequence */
            while (n > 0 && (buf[--n] & 0xC0) == 0x80) {}
            buf[n] = 0;
        }
        /* pad by display width: readline lays out columns by it, not bytes */
        printf("%s%*s\n", buf, line > dwidth(buf) ? line - dwidth(buf) : 0, "");
    }
}

static void emit_cmd_output(const char *shell, const char *suffix, int space, int tree) {
    char line[1024];
    size_t clen = strlen(g_cur);
    FILE *f = popen(shell, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *desc, *slash;
        line[strcspn(line, "\r\n")] = 0;
        if (!*line || strstr(line, "<none>")) continue;
        /* "name<TAB>description" */
        if ((desc = strchr(line, '\t'))) *desc++ = 0;
        if (!*line) continue;
        /* tree: fold "a/b" into "a/" past the typed prefix, like paths */
        if (tree && !strncmp(line, g_cur, clen) && (slash = strchr(line + clen, '/'))) {
            slash[1] = 0;
            emit(line, 0);
            continue;
        }
        if (suffix) {
            char buf[1100];
            snprintf(buf, sizeof buf, "%s%s", line, suffix);
            emit_desc(buf, space, desc);
        } else {
            emit_desc(line, space, desc);
        }
    }
    pclose(f);
}

static void complete_files(int dirs_only) {
    char dir[2048], full[4096];
    const char *slash = strrchr(g_cur, '/');
    const char *base = slash ? slash + 1 : g_cur;
    DIR *d;
    struct dirent *e;
    size_t dlen = slash ? (size_t)(slash - g_cur) + 1 : 0;

    snprintf(dir, sizeof dir, "%.*s", (int)dlen, g_cur);
    if (dir[0] == '~' && getenv("HOME")) {
        snprintf(full, sizeof full, "%s%s", getenv("HOME"), dir + 1);
        snprintf(dir, sizeof dir, "%s", full);
    }
    d = opendir(dlen ? dir : ".");
    if (!d) return;
    while ((e = readdir(d))) {
        struct stat st;
        char path[4096], cand[4096];
        int isdir;
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (e->d_name[0] == '.' && base[0] != '.') continue;
        if (strncmp(e->d_name, base, strlen(base)) != 0) continue;
        snprintf(path, sizeof path, "%s%s", dlen ? dir : "", e->d_name);
        isdir = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
        if (dirs_only && !isdir) continue;
        snprintf(cand, sizeof cand, "%.*s%s%s", (int)dlen, g_cur, e->d_name, isdir ? "/" : "");
        emit(cand, !isdir);
    }
    closedir(d);
}

/* Text of a "# ..." comment line without the #s and surrounding blanks. */
static char *comment_text(char *s) {
    size_t n;
    while (*s == '#') s++;
    while (*s == ' ' || *s == '\t') s++;
    n = strlen(s);
    while (n && strchr(" \t\r\n", s[n - 1])) s[--n] = 0;
    return s;
}

/* Emit explicit targets of a makefile: "name [name...]: deps", skipping
 * variable assignments, recipes, special (.PHONY) and pattern (%) rules.
 * A target's description is its "## text" after the deps, or else the
 * comment lines directly above it. */
/* Open the makefile make would read, honouring -f/--file/--makefile and
 * -C/--directory seen on the line. */
static FILE *open_makefile(void) {
    static const char *defaults[] = {"GNUmakefile", "makefile", "Makefile", 0};
    char path[4096];
    const char **d;
    FILE *f = NULL;
    const char *g_makefile = seen("-f", "--file", "--makefile");
    const char *g_makedir = seen("-C", "--directory", NULL);

    if (g_makefile) {
        if (g_makedir && g_makefile[0] != '/')
            snprintf(path, sizeof path, "%s/%s", g_makedir, g_makefile);
        else
            snprintf(path, sizeof path, "%s", g_makefile);
        f = fopen(path, "r");
    }
    for (d = defaults; !g_makefile && !f && *d; d++) {
        snprintf(path, sizeof path, "%s%s%s", g_makedir ? g_makedir : "", g_makedir ? "/" : "", *d);
        f = fopen(path, "r");
    }
    return f;
}

static void complete_make_targets(void) {
    char line[4096], comment[1024] = "";
    FILE *f = open_makefile();
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *colon = strchr(line, ':'), *tok, *inline_doc, *desc;
        if (line[0] == '#') { /* collect the comment block above a target */
            char *t = comment_text(line);
            size_t have = strlen(comment);
            if (*t) snprintf(comment + have, sizeof comment - have, "%s%s", have ? " " : "", t);
            continue;
        }
        desc = NULL;
        if (line[0] != '\t' && colon && colon[1] != '=' && !(colon[1] == ':' && colon[2] == '=')) {
            if ((inline_doc = strstr(colon, "##"))) desc = comment_text(inline_doc);
            else if (*comment) desc = comment;
            *colon = 0;
            if (!strpbrk(line, "=$%")) {
                for (tok = strtok(line, " \t"); tok; tok = strtok(NULL, " \t"))
                    if (tok[0] != '.') emit_desc(tok, 1, desc && *desc ? strdup(desc) : NULL);
            }
        }
        comment[0] = 0; /* a comment only documents the line right below it */
    }
    fclose(f);
}

/* Overridable-looking variable name: capitals, digits and _ only, so
 * internal lowercase helpers and special ".NAME" variables are skipped. */
static int upper_name(const char *s, const char *end) {
    for (; s < end; s++)
        if (!((*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || *s == '_')) return 0;
    return 1;
}

/* Emit "NAME=" for each upper-case variable assigned in the makefile (NAME =
 * / := / ::= / ?= / += / != value, optionally after "export"/"override"), for
 * command-line overrides like "make CC=clang". Description as for targets. */
static void complete_make_variables(void) {
    char line[4096], comment[1024] = "";
    FILE *f = open_makefile();
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *p = line, *name, *end, *doc, *desc = NULL;
        if (line[0] == '#') {
            char *t = comment_text(line);
            size_t have = strlen(comment);
            if (*t) snprintf(comment + have, sizeof comment - have, "%s%s", have ? " " : "", t);
            continue;
        }
        if (line[0] != '\t') {
            for (;;) { /* skip export/override prefixes */
                while (*p == ' ') p++;
                if (!strncmp(p, "export ", 7)) p += 7;
                else if (!strncmp(p, "override ", 9)) p += 9;
                else break;
            }
            name = p;
            while (*p && !strchr(" \t:?+!=#$%()", *p)) p++;
            end = p;
            while (*p == ' ' || *p == '\t') p++;
            if (end > name && upper_name(name, end) && (*p == '=' || !strncmp(p, ":=", 2) || !strncmp(p, "::=", 3) ||
                               !strncmp(p, "?=", 2) || !strncmp(p, "+=", 2) || !strncmp(p, "!=", 2))) {
                char buf[1024], full[2048], *op = p, *val = strchr(p, '=') + 1;
                size_t n;
                if ((doc = strstr(val, "##"))) { *doc = 0; desc = comment_text(doc + 2); }
                else if (*comment) desc = comment;
                /* the assigned value, shown as "C compiler · ?= cc" */
                while (*val == ' ' || *val == '\t') val++;
                n = strlen(val);
                while (n && strchr(" \t\r\n\\", val[n - 1])) val[--n] = 0;
                if (*val)
                    snprintf(full, sizeof full, "%s%s%.*s %s", desc && *desc ? desc : "",
                             desc && *desc ? " · " : "", (int)(strchr(op, '=') - op + 1), op, val);
                else
                    snprintf(full, sizeof full, "%s", desc ? desc : "");
                snprintf(buf, sizeof buf, "%.*s=", (int)(end - name), name);
                emit_desc(buf, 0, *full ? strdup(full) : NULL);
            }
        }
        comment[0] = 0;
    }
    fclose(f);
}

/* "description" of a command or flag spec, if any. */
static const char *spec_desc(const ynode *spec) {
    const ynode *d = yaml_get(spec, "description");
    return d && d->type == Y_STR ? d->str : NULL;
}

/* "groups: {"Up": {description?}}": the group (own level first, then
 * root) whose key holds the letter of short flag "-ch". Its flags are
 * collapsed into one "-📌 Up" entry. */
static const ynode *find_group(const ynode *c, const ynode *root, char ch) {
    const ynode *s = c, *g;
    for (;;) {
        for (g = yaml_get(s, "groups") ? yaml_get(s, "groups")->child : NULL; g; g = g->next)
            if (g->key && strchr(g->key, ch)) return g;
        if (s == root) return NULL;
        s = root;
    }
}

/* `check`: warn about "groups" letters without a "-x" flag (own or root). */
static int check_groups(const ynode *n, const ynode *root, const char *path) {
    const ynode *g, *c;
    const char *p;
    int bad = 0;
    for (g = yaml_get(n, "groups") ? yaml_get(n, "groups")->child : NULL; g; g = g->next)
        for (p = g->key; p && *p; p++) {
            char flag[3] = {'-', *p, 0};
            const ynode *own = yaml_get(n, "flags"), *top = yaml_get(root, "flags");
            if (!yaml_get(own, flag) && !yaml_get(top, flag)) {
                fprintf(stderr, "%s: groups \"%s\": no flag %s\n", path, g->key, flag);
                bad = 1;
            }
        }
    for (c = yaml_get(n, "commands") ? yaml_get(n, "commands")->child : NULL; c; c = c->next) {
        char sub[512];
        snprintf(sub, sizeof sub, "%s %s", path, c->key);
        bad |= check_groups(c, root, sub);
    }
    return bad;
}


/* "when: {file, contains?}": true if `file` (relative to cwd) exists and,
 * when given, contains the `contains` string. No "when" = always true. */
static int when_ok(const ynode *spec) {
    const ynode *w = yaml_get(spec, "when"), *f, *s;
    FILE *fp;
    char *buf;
    long n;
    int ok;
    if (!w) return 1;
    f = yaml_get(w, "file");
    s = yaml_get(w, "contains");
    if (!f || f->type != Y_STR || !(fp = fopen(f->str, "rb"))) return 0;
    if (!s || s->type != Y_STR) { fclose(fp); return 1; }
    fseek(fp, 0, SEEK_END);
    n = ftell(fp);
    rewind(fp);
    if (n < 0 || !(buf = malloc((size_t)n + 1))) { fclose(fp); return 0; }
    buf[fread(buf, 1, (size_t)n, fp)] = 0;
    fclose(fp);
    ok = strstr(buf, s->str) != NULL;
    free(buf);
    return ok;
}

/* Emit every subcommand of `cmds` whose "when" holds. "aliases" are not
 * listed, only shown in the description ("desc (co, ch)"); an alias typed
 * in full is emitted. */
static void emit_commands(const ynode *cmds) {
    const ynode *c, *a, *al;
    g_kind = "command";
    for (c = cmds ? cmds->child : NULL; c; c = c->next) {
        const char *d = spec_desc(c);
        char buf[1024];
        size_t len;
        if (!when_ok(c)) continue;
        al = yaml_get(c, "aliases");
        al = al && al->type == Y_ARR ? al->child : NULL;
        if (!al) { emit_desc(c->key, 1, d); continue; }
        len = (size_t)snprintf(buf, sizeof buf, "%s%s(", d ? d : "", d ? " " : "");
        for (a = al; a && len < sizeof buf; a = a->next)
            if (a->type == Y_STR) {
                len += (size_t)snprintf(buf + len, sizeof buf - len, "%s%s",
                                        buf[len - 1] == '(' ? "" : ", ", a->str);
                if (!strcmp(a->str, g_cur)) emit_desc(a->str, 1, d);
            }
        if (len < sizeof buf - 1) strcat(buf, ")");
        emit_desc(c->key, 1, strdup(buf));
    }
}

/* Subcommand `name` of `cmds`, matched by key or by one of its "aliases". */
static const ynode *find_command(const ynode *cmds, const char *name) {
    const ynode *c, *a;
    for (c = cmds ? cmds->child : NULL; c; c = c->next) {
        if (!strcmp(c->key, name)) return c;
        a = yaml_get(c, "aliases");
        for (a = a && a->type == Y_ARR ? a->child : NULL; a; a = a->next)
            if (a->type == Y_STR && !strcmp(a->str, name)) return c;
    }
    return NULL;
}

static int is_str(const ynode *v, const char *s) { return v && v->type == Y_STR && !strcmp(v->str, s); }

/* Executables on $PATH (a word with '/' completes as a path). */
static void complete_commands(void) {
    char *path, *dir, *save;
    if (strchr(g_cur, '/')) { g_kind = "file"; complete_files(0); return; }
    if (!getenv("PATH") || !(path = strdup(getenv("PATH")))) return;
    g_kind = "command";
    for (dir = strtok_r(path, ":", &save); dir; dir = strtok_r(NULL, ":", &save)) {
        DIR *d = opendir(*dir ? dir : ".");
        struct dirent *e;
        char full[4096];
        struct stat st;
        if (!d) continue;
        while ((e = readdir(d))) {
            if (e->d_name[0] == '.' || strncmp(e->d_name, g_cur, strlen(g_cur))) continue;
            snprintf(full, sizeof full, "%s/%s", dir, e->d_name);
            if (!stat(full, &st) && !S_ISDIR(st.st_mode) && !access(full, X_OK)) emit(e->d_name, 1);
        }
        closedir(d);
    }
    free(path);
}

/* Complete a <value> from a spec: null, "any"/"file"/"dir", "@builtin",
 * {"exec": ..., "suffix": ..., "nospace": ...}, or an array (union). */
static void complete_value(const ynode *v, const ynode *parent) {
    if (!v) return;
    switch (v->type) {
    case Y_ARR: {
        const ynode *e;
        for (e = v->child; e; e = e->next) complete_value(e, parent);
        break;
    }
    case Y_STR:
        if (!strcmp(v->str, "file")) { g_kind = "file"; complete_files(0); }
        else if (!strcmp(v->str, "dir")) { g_kind = "dir"; complete_files(1); }
        else if (!strcmp(v->str, "@make-targets")) { g_kind = "target"; complete_make_targets(); }
        else if (!strcmp(v->str, "@make-variables")) { g_kind = "var"; complete_make_variables(); }
        else if (!strcmp(v->str, "@subcommands")) emit_commands(yaml_get(parent, "commands"));
        else if (!strcmp(v->str, "@command")) complete_commands();
        break;
    case Y_OBJ: {
        const ynode *ex = yaml_get(v, "exec"), *sx = yaml_get(v, "suffix"), *ns = yaml_get(v, "nospace"),
                    *tr = yaml_get(v, "tree");
        char shell[4096];
        if (!ex || ex->type != Y_STR) break;
        snprintf(shell, sizeof shell, "{ %s\n} 2>/dev/null", ex->str);
        g_kind = NULL;
        emit_cmd_output(shell, sx && sx->type == Y_STR ? sx->str : NULL,
                        !(ns && ns->type == Y_BOOL && ns->b), tr && tr->type == Y_BOOL && tr->b);
        break;
    }
    default: break;
    }
}

/* Find flag `name` in cmd's flags; *val receives its "value" spec (NULL
 * when the flag takes no value). Flags are maps: {value, description}. */
static int find_flag(const ynode *cmd, const char *name, const ynode **val) {
    const ynode *f = yaml_get(yaml_get(cmd, "flags"), name);
    if (f) *val = yaml_get(f, "value");
    return f != NULL;
}

static int takes_value(const ynode *v) { return v && v->type != Y_NULL; }

/* ------------------------------------------------------------ parsing */

#define MAXW 256

/* Split a command line like bash would (roughly). Returns word count; the
 * last word is the one being completed (may be empty). start[i] receives
 * the offset of word i in the line. */
static int split(const char *line, char **words, size_t *start) {
    int n = 0;
    const char *p = line;
    char buf[4096];
    size_t b;
    for (;;) {
        char q = 0;
        while (*p == ' ' || *p == '\t') p++;
        start[n] = (size_t)(p - line);
        b = 0;
        if (!*p) { words[n++] = strdup(""); break; }
        while (*p && (q || (*p != ' ' && *p != '\t'))) {
            if (q) {
                if (*p == q) q = 0;
                else if (q == '"' && *p == '\\' && p[1]) buf[b++] = *++p;
                else buf[b++] = *p;
            } else if (*p == '\'' || *p == '"') q = *p;
            else if (*p == '\\' && p[1]) buf[b++] = *++p;
            else buf[b++] = *p;
            p++;
            if (b >= sizeof buf - 2) break;
        }
        buf[b] = 0;
        words[n++] = strdup(buf);
        if (!*p || n >= MAXW - 1) break;
    }
    return n;
}

static int do_complete(const char *line, const char *wbreaks) {
    char *w[MAXW];
    size_t start[MAXW];
    int n = split(line, w, start), i;
    const ynode *root, *c, *parent = NULL, *v = NULL;
    const char *cur, *slash, *cname;

    g_wbreaks = wbreaks;
    g_raw = line + start[n - 1];
    cur = g_cur = w[n - 1];

    slash = strrchr(w[0], '/');
    cname = slash ? slash + 1 : w[0];
    if (!(root = c = load_spec(cname))) {
        if (n > 1) { g_kind = "file"; complete_files(0); } /* sudo cmd-without-spec */
        return 0;
    }

    /* walk the already-typed words */
    for (i = 1; i < n - 1; i++) {
        const ynode *s;
        if (!strcmp(w[i], "--")) continue;
        if (w[i][0] == '-') {
            if (strchr(w[i], '=')) continue;
            if ((find_flag(c, w[i], &v) || find_flag(root, w[i], &v)) && takes_value(v)) {
                if (i + 1 < n - 1 && g_nseen < MAXSEEN) {
                    g_seen_flag[g_nseen] = w[i];
                    g_seen_val[g_nseen++] = w[i + 1];
                }
                i++; /* skip its value */
            }
            continue;
        }
        if ((s = find_command(yaml_get(c, "commands"), w[i]))) { parent = c; c = s; cname = w[i]; continue; }
        if (is_str(yaml_get(c, "args"), "@command")) {
            if (strchr(w[i], '=')) continue; /* env NAME=value cmd */
            /* the rest of the line is a command line of its own */
            g_nseen = 0;
            return do_complete(line + start[i], wbreaks);
        }
    }

    /* value for previous flag? */
    if (n >= 3 && w[n - 2][0] == '-' && !strchr(w[n - 2], '=')) {
        if ((find_flag(c, w[n - 2], &v) || find_flag(root, w[n - 2], &v)) && takes_value(v)) {
            complete_value(v, c);
            return 0;
        }
    }

    /* --flag=value: '=' is a wordbreak, so complete only the part after it */
    if (cur[0] == '-' && cur[1] == '-' && strchr(cur, '=')) {
        char name[256];
        const char *eq = strchr(cur, '='), *req = strchr(g_raw, '=');
        snprintf(name, sizeof name, "%.*s", (int)(eq - cur), cur);
        if (find_flag(c, name, &v) || find_flag(root, name, &v)) {
            g_cur = eq + 1;
            if (req) g_raw = req + 1;
            complete_value(v, c);
        }
        return 0;
    }

    if (cur[0] == '-') {
        /* own flags, then root flags (valid at every depth); "-Dx=" takes
         * its value attached, so no space */
        const ynode *f, *s = c;
        for (;;) {
            for (f = yaml_get(s, "flags") ? yaml_get(s, "flags")->child : NULL; f; f = f->next) {
                const ynode *gr = f->key[1] && !f->key[2] ? find_group(c, root, f->key[1]) : NULL;
                g_group = gr ? gr->key : NULL;
                g_gdesc = gr ? spec_desc(gr) : NULL;
                g_show = g_group                                    ? SHOW_COLLAPSED
                         : is_str(yaml_get(f, "show"), "always")      ? SHOW_ALWAYS
                         : is_str(yaml_get(f, "show"), "collapsed") ? SHOW_COLLAPSED
                                                                    : SHOW_DEFAULT;
                emit_desc(f->key, f->key[strlen(f->key) - 1] != '=', spec_desc(f));
            }
            g_show = SHOW_DEFAULT;
            g_group = g_gdesc = NULL;
            if (s == root) break;
            s = root;
        }
        return 0;
    }

    emit_commands(yaml_get(c, "commands"));
    g_examples = yaml_get(c, "examples");
    g_examples_title = spec_desc(c);
    g_examples_name = cname;
    v = yaml_get(c, "args");
    /* "@subcommands" on a leaf (docker help) lists the parent's commands */
    complete_value(v, is_str(v, "@subcommands") && parent ? parent : c);
    return 0;
}

/* ----------------------------------------------------------------- main */

/* Call fn for every unique spec name (first dir wins). */
static void each_spec(void (*fn)(const char *)) {
    char *names[512];
    int nn = 0, i, j;
    for (i = 0; i < g_ndirs; i++) {
        DIR *d = opendir(g_dirs[i]);
        struct dirent *e;
        if (!d) continue;
        while ((e = readdir(d)) && nn < 512) {
            size_t len = strlen(e->d_name), xl = 0;
            int x;
            if (e->d_name[0] == '.') continue;
            for (x = 0; g_exts[x]; x++) {
                xl = strlen(g_exts[x]);
                if (len > xl && !strcmp(e->d_name + len - xl, g_exts[x])) break;
            }
            if (!g_exts[x]) continue;
            e->d_name[len - xl] = 0;
            for (j = 0; j < nn && strcmp(names[j], e->d_name); j++) {}
            if (j == nn) names[nn++] = strdup(e->d_name);
        }
        closedir(d);
    }
    for (i = 0; i < nn; i++) fn(names[i]);
}

static int g_ncomplete;
static const char *g_self;
static void print_complete(const char *name) {
    const char *p;
    if (!g_ncomplete++) {
        /* -C string is eval'd by bash: backslash-escape the path inside it,
           then single-quote the whole string for the `complete` line */
        fputs("complete -o nospace -C '", stdout);
        for (p = g_self; *p; p++) {
            if (!isalnum((unsigned char)*p) && !strchr("/._-+,", *p)) putchar('\\');
            if (*p == '\'') fputs("'\\''", stdout);
            else putchar(*p);
        }
        fputs(" _complete'", stdout);
    }
    printf(" %s", name);
}
static void print_name(const char *name) { puts(name); }

static int g_cache_failed;

/* `autocomplete cache`: compile a big spec (> CACHE_MIN) unconditionally. */
static void cache_spec(const char *name) {
    char path[PATH_MAX], bin[PATH_MAX], err[256];
    struct stat st;
    ynode *n;
    if (!find_spec(name, path, sizeof path)) {
        fprintf(stderr, "%s: no such spec\n", name);
        g_cache_failed = 1;
        return;
    }
    if (stat(path, &st) == 0 && st.st_size <= CACHE_MIN) return;
    if (!(n = yaml_load(path, err, sizeof err))) {
        fprintf(stderr, "%s: %s\n", path, err);
        g_cache_failed = 1;
        return;
    }
    if (save_cache(name, n, path, bin, sizeof bin)) printf("%s -> %s\n", path, bin);
    else {
        fprintf(stderr, "%s: cannot write cache%s\n", name, getenv("AUTOCOMPLETE_NOCACHE") ? " (AUTOCOMPLETE_NOCACHE set)" : "");
        g_cache_failed = 1;
    }
    yaml_free(n);
}

/* A plain command name usable as a spec file name. */
static int valid_name(const char *s) {
    const char *p;
    for (p = s; *p && (isalnum((unsigned char)*p) || strchr("._+-", *p)); p++) {}
    return !*p && *s && s[0] != '.' && s[0] != '-';
}

/* After specs were added or removed: the shell must re-register names. */
static void reload_hint(void) {
    printf("\nRun 'eval \"$(autocomplete bash)\"' (or open a new shell) to use it now.\n\n");
}

/* `autocomplete alias --rm NAME...`: remove alias symlinks (never real specs). */
static int alias_rm(const char *dir, int argc, char **argv) {
    char path[PATH_MAX], target[PATH_MAX];
    int i, rc = 0, removed = 0;
    for (i = 0; i < argc; i++) {
        if (!valid_name(argv[i])) {
            fprintf(stderr, "%s: not a command name\n", argv[i]);
            rc = 1;
            continue;
        }
        snprintf(path, sizeof path, "%s/%s.yaml", dir, argv[i]);
        if (readlink(path, target, 1) < 0) {
            fprintf(stderr, "%s: not an alias\n", argv[i]);
            rc = 1;
        } else if (unlink(path)) {
            perror(path);
            rc = 1;
        } else {
            printf("removed %s\n", path);
            removed++;
        }
    }
    if (removed) reload_hint();
    return rc;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* `autocomplete alias --list`: print "NAME -> CMD" for each alias symlink,
 * CMD being the target's file name without .yaml/.yml. */
static int alias_list(const char *dir) {
    char path[PATH_MAX], target[PATH_MAX], *names[4096], *base, *dot;
    struct dirent *e;
    size_t i, n = 0;
    ssize_t len;
    DIR *d = opendir(dir);
    if (!d) return 0;
    while ((e = readdir(d)) && n < sizeof names / sizeof *names) {
        dot = strrchr(e->d_name, '.');
        if (!dot || (strcmp(dot, ".yaml") && strcmp(dot, ".yml"))) continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (readlink(path, target, 1) >= 0) names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, n, sizeof *names, cmp_str);
    for (i = 0; i < n; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        len = readlink(path, target, sizeof target - 1);
        target[len < 0 ? 0 : len] = 0;
        base = strrchr(target, '/');
        base = base ? base + 1 : target;
        if ((dot = strrchr(base, '.'))) *dot = 0;
        *strrchr(names[i], '.') = 0;
        printf("%s -> %s\n", names[i], base);
        free(names[i]);
    }
    return 0;
}

/* `autocomplete alias [--force] CMD NAME...`: symlink
 * ~/.config/autocomplete/NAME.yaml to CMD's spec for each NAME. */
static int alias_cmds(int argc, char **argv) {
    char dir[PATH_MAX], path[PATH_MAX], target[PATH_MAX], abs[PATH_MAX];
    const char *cmd = NULL;
    int i, force = 0, rc = 0, n = 0, made = 0;
    int rm = argc >= 1 && !strcmp(argv[0], "--rm");
    int list = argc == 1 && !strcmp(argv[0], "--list");
    for (i = 0; i < argc && !rm && !list; i++)
        if (!strcmp(argv[i], "--force")) force = 1;
        else if (!cmd) cmd = argv[i];
        else n++;
    if (!list && (rm ? argc < 2 : !n)) {
        fprintf(stderr, "usage: autocomplete alias [--force] CMD NAME...\n"
                        "       autocomplete alias --rm NAME...\n"
                        "       autocomplete alias --list\n");
        return 1;
    }
    if (!getenv("HOME")) {
        fprintf(stderr, "alias: HOME is not set\n");
        return 1;
    }
    snprintf(dir, sizeof dir, "%s/.config/autocomplete", getenv("HOME"));
    if (list) return alias_list(dir);
    if (rm) return alias_rm(dir, argc - 1, argv + 1);
    if (!valid_name(cmd)) {
        fprintf(stderr, "%s: not a command name\n", cmd);
        return 1;
    }
    if (!find_spec(cmd, target, sizeof target) || !realpath(target, abs)) {
        fprintf(stderr, "%s: no such spec\n", cmd);
        return 1;
    }
    mkdirs(dir);
    for (i = 0; i < argc; i++) {
        if (!strcmp(argv[i], "--force") || argv[i] == cmd) continue;
        if (!valid_name(argv[i])) {
            fprintf(stderr, "%s: not a command name\n", argv[i]);
            rc = 1;
            continue;
        }
        snprintf(path, sizeof path, "%s/%s.yaml", dir, argv[i]);
        if (!strcmp(path, abs)) {
            fprintf(stderr, "%s: alias of itself\n", argv[i]);
            rc = 1;
            continue;
        }
        if (access(path, F_OK) == 0 || readlink(path, target, 1) >= 0) {
            if (!force) {
                fprintf(stderr, "%s: exists (use --force to overwrite)\n", path);
                rc = 1;
                continue;
            }
            unlink(path);
        }
        if (symlink(abs, path)) {
            perror(path);
            rc = 1;
        } else {
            printf("%s -> %s\n", path, abs);
            made++;
        }
    }
    if (made) reload_hint();
    return rc;
}

/* `autocomplete from man [--force] CMD...`: write ~/.config/autocomplete/CMD.yaml
 * from the man page. */
static int man_cmds(int argc, char **argv) {
    char dir[PATH_MAX], path[PATH_MAX];
    int i, force = 0, rc = 0, made = 0;
    if (!getenv("HOME")) {
        fprintf(stderr, "man: HOME is not set\n");
        return 1;
    }
    snprintf(dir, sizeof dir, "%s/.config/autocomplete", getenv("HOME"));
    mkdirs(dir);
    for (i = 0; i < argc; i++)
        if (!strcmp(argv[i], "--force")) force = 1;
    for (i = 0; i < argc; i++) {
        ynode *n;
        FILE *f;
        if (!strcmp(argv[i], "--force")) continue;
        if (!valid_name(argv[i])) {
            fprintf(stderr, "%s: not a command name\n", argv[i]);
            rc = 1;
            continue;
        }
        snprintf(path, sizeof path, "%s/%s.yaml", dir, argv[i]);
        if (!force && access(path, F_OK) == 0) {
            fprintf(stderr, "%s: exists (use --force to overwrite)\n", path);
            rc = 1;
            continue;
        }
        if (!(n = man_spec(argv[i]))) {
            fprintf(stderr, "%s: no man page with options\n", argv[i]);
            rc = 1;
            continue;
        }
        if (!(f = fopen(path, "w"))) {
            perror(path);
            rc = 1;
        } else {
            fprintf(f, "# generated by `autocomplete from man %s`; edit freely\n", argv[i]);
            yaml_write(f, n);
            fclose(f);
            puts(path);
            made++;
        }
        yaml_free(n);
    }
    if (made) reload_hint();
    return rc;
}

static void print_bash(const char *self) {
    g_self = self;
    each_spec(print_complete);
    if (g_ncomplete) putchar('\n');
}

/* `complete -C` passes the line in $COMP_LINE, the cursor (in characters) in
   $COMP_POINT; COMP_WORDBREAKS isn't exported, so it is used only if the user
   exported it. */
static int complete_env(void) {
    const char *line = getenv("COMP_LINE"), *pt = getenv("COMP_POINT");
    const char *wb = getenv("COMP_WORDBREAKS");
    char *buf;
    size_t i = 0;
    long n;
    int rc;
    if (!line) return 1;
    buf = strdup(line);
    if (pt) {
        for (n = atol(pt); n > 0 && buf[i]; n--)
            for (i++; (buf[i] & 0xC0) == 0x80; i++) {}
        buf[i] = 0;
    }
    rc = do_complete(buf, wb ? wb : "\"'@><=;|&(:");
    flush();
    free(buf);
    return rc;
}

/* Print a parsed spec as JSON (for `autocomplete check`). */
static void dump(const ynode *n, int ind) {
    const ynode *c;
    const char *s;
    switch (n->type) {
    case Y_NULL: printf("null"); break;
    case Y_BOOL: printf(n->b ? "true" : "false"); break;
    case Y_STR:
        putchar('"');
        for (s = n->str; *s; s++) {
            if (*s == '"' || *s == '\\') printf("\\%c", *s);
            else if (*s == '\n') printf("\\n");
            else if (*s == '\t') printf("\\t");
            else putchar(*s);
        }
        putchar('"');
        break;
    case Y_ARR:
    case Y_OBJ:
        putchar(n->type == Y_OBJ ? '{' : '[');
        for (c = n->child; c; c = c->next) {
            printf("\n%*s", ind + 2, "");
            if (n->type == Y_OBJ) {
                ynode k;
                memset(&k, 0, sizeof k);
                k.type = Y_STR;
                k.str = c->key;
                dump(&k, 0);
                printf(": ");
            }
            dump(c, ind + 2);
            if (c->next) putchar(',');
        }
        if (n->child) printf("\n%*s", ind, "");
        putchar(n->type == Y_OBJ ? '}' : ']');
        break;
    }
}

int main(int argc, char **argv) {
    init_dirs();
    if (argc >= 2 && !strcmp(argv[1], "bash")) {
        char abs[PATH_MAX];
        /* bare name -> found via PATH at completion time; otherwise pin it */
        print_bash(strchr(argv[0], '/') && realpath(argv[0], abs) ? abs : argv[0]);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "_complete")) return complete_env();
    if (argc >= 3 && !strcmp(argv[1], "check")) {
        char err[256];
        ynode *n = yaml_load(argv[2], err, sizeof err);
        if (!n) {
            fprintf(stderr, "%s: %s\n", argv[2], err);
            return 1;
        }
        dump(n, 0);
        putchar('\n');
        return check_groups(n, n, argv[2]);
    }
    if (argc >= 2 && !strcmp(argv[1], "cache")) {
        int i;
        if (argc == 2) each_spec(cache_spec);
        for (i = 2; i < argc; i++) cache_spec(argv[i]);
        return g_cache_failed;
    }
    if (argc >= 2 && !strcmp(argv[1], "alias")) return alias_cmds(argc - 2, argv + 2);
    if (argc >= 4 && !strcmp(argv[1], "from") && !strcmp(argv[2], "man"))
        return man_cmds(argc - 3, argv + 3);
    if (argc >= 2 && !strcmp(argv[1], "list")) {
        each_spec(print_name);
        return 0;
    }
    fprintf(stderr,
            "usage: autocomplete bash              print bash init script\n"
            "       autocomplete list              list available specs\n"
            "       autocomplete check FILE        validate a spec, print it as JSON\n"
            "       autocomplete cache [NAME...]   precompile big specs (> 100 KB) into the cache\n"
            "       autocomplete from man [--force] CMD...   write ~/.config/autocomplete/CMD.yaml from man CMD\n"
            "       autocomplete alias [--force] CMD NAME...  complete NAME like CMD (symlinks its spec)\n"
            "       autocomplete alias --rm NAME...    remove those aliases\n"
            "       autocomplete alias --list          list aliases (NAME -> CMD)\n"
            "       autocomplete _complete LINE [WORDBREAKS]\n");
    return 2;
}
