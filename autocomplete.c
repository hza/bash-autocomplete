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
 * `autocomplete man CMD` writes a spec for CMD from its man page.
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
} cand;
static cand g_cands[MAXCANDS];
static int g_ncands;
static const char *g_kind; /* kind given to candidates emitted now */

/* Escape for bash and queue, trimmed to bash's current word. Duplicates
 * (union values can overlap) are dropped, keeping the first description. */
static void emit_desc(const char *cand, int space, const char *desc) {
    char esc[4096];
    size_t n = 0;
    const char *p;
    if (strncmp(cand, g_cur, strlen(g_cur)) != 0) return;
    for (p = cand; *p && n < sizeof esc - 3; p++) {
        if (strchr(" \t\\'\"()<>&;|*?[]$`!#{}~", *p)) esc[n++] = '\\';
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
                if (!g_cands[i].desc) g_cands[i].desc = desc;
                return;
            }
    }
    if (g_ncands < MAXCANDS) {
        g_cands[g_ncands].text = strdup(p);
        g_cands[g_ncands].space = space;
        g_cands[g_ncands].kind = g_kind;
        g_cands[g_ncands++].desc = desc;
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
    emit_desc(" 🔧 vars", 0, strdup(names));
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

static void flush(void) {
    int i, any = 0, width = 0, kwidth = 0, mixed = 0, cols, line;
    collapse_vars();
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
        if (n > cols - 2) { /* avoid wrapping; don't split a UTF-8 sequence */
            n = cols - 2;
            while (n > 0 && (buf[n] & 0xC0) == 0x80) n--;
            buf[n] = 0;
        }
        printf("%-*s\n", line, buf);
    }
}

static void emit_cmd_output(const char *shell, const char *suffix, int space) {
    char line[1024];
    FILE *f = popen(shell, "r");
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        if (!*line || strstr(line, "<none>")) continue;
        if (suffix) {
            char buf[1100];
            snprintf(buf, sizeof buf, "%s%s", line, suffix);
            emit(buf, space);
        } else {
            emit(line, space);
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

/* Emit every subcommand of `cmds`, plus each of its "aliases". */
static void emit_commands(const ynode *cmds) {
    const ynode *c, *a;
    g_kind = "command";
    for (c = cmds ? cmds->child : NULL; c; c = c->next) {
        emit_desc(c->key, 1, spec_desc(c));
        a = yaml_get(c, "aliases");
        for (a = a && a->type == Y_ARR ? a->child : NULL; a; a = a->next)
            if (a->type == Y_STR) emit_desc(a->str, 1, spec_desc(c));
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
        break;
    case Y_OBJ: {
        const ynode *ex = yaml_get(v, "exec"), *sx = yaml_get(v, "suffix"), *ns = yaml_get(v, "nospace");
        char shell[4096];
        if (!ex || ex->type != Y_STR) break;
        snprintf(shell, sizeof shell, "{ %s\n} 2>/dev/null", ex->str);
        g_kind = NULL;
        emit_cmd_output(shell, sx && sx->type == Y_STR ? sx->str : NULL,
                        !(ns && ns->type == Y_BOOL && ns->b));
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
 * last word is the one being completed (may be empty). raw_start receives
 * the offset of the last word in the line. */
static int split(const char *line, char **words, size_t *raw_start) {
    int n = 0;
    const char *p = line;
    char buf[4096];
    size_t b;
    for (;;) {
        char q = 0;
        while (*p == ' ' || *p == '\t') p++;
        *raw_start = (size_t)(p - line);
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
    size_t raw;
    int n = split(line, w, &raw), i;
    const ynode *root, *c, *parent = NULL, *v = NULL;
    const char *cur, *slash;

    g_wbreaks = wbreaks;
    g_raw = line + raw;
    cur = g_cur = w[n - 1];

    slash = strrchr(w[0], '/');
    if (!(root = c = load_spec(slash ? slash + 1 : w[0]))) return 0;

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
        if ((s = find_command(yaml_get(c, "commands"), w[i]))) { parent = c; c = s; }
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
        const ynode *f;
        for (f = yaml_get(c, "flags") ? yaml_get(c, "flags")->child : NULL; f; f = f->next)
            emit_desc(f->key, 1, spec_desc(f));
        return 0;
    }

    emit_commands(yaml_get(c, "commands"));
    v = yaml_get(c, "args");
    /* "@subcommands" on a leaf (docker help) lists the parent's commands */
    complete_value(v, v && v->type == Y_STR && !strcmp(v->str, "@subcommands") && parent ? parent : c);
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

static void print_complete(const char *name) { printf("complete -o nospace -F _autocomplete %s\n", name); }
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

/* `autocomplete man [-f] CMD...`: write ~/.config/autocomplete/CMD.yaml
 * from the man page. */
static int man_cmds(int argc, char **argv) {
    char dir[PATH_MAX], path[PATH_MAX];
    int i, force = 0, rc = 0;
    if (!getenv("HOME")) {
        fprintf(stderr, "man: HOME is not set\n");
        return 1;
    }
    snprintf(dir, sizeof dir, "%s/.config/autocomplete", getenv("HOME"));
    mkdirs(dir);
    for (i = 0; i < argc; i++)
        if (!strcmp(argv[i], "-f")) force = 1;
    for (i = 0; i < argc; i++) {
        const char *p;
        ynode *n;
        FILE *f;
        if (!strcmp(argv[i], "-f")) continue;
        for (p = argv[i]; *p && (isalnum((unsigned char)*p) || strchr("._+-", *p)); p++) {}
        if (*p || !*argv[i] || argv[i][0] == '.' || argv[i][0] == '-') {
            fprintf(stderr, "%s: not a command name\n", argv[i]);
            rc = 1;
            continue;
        }
        snprintf(path, sizeof path, "%s/%s.yaml", dir, argv[i]);
        if (!force && access(path, F_OK) == 0) {
            fprintf(stderr, "%s: exists (use -f to overwrite)\n", path);
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
            fprintf(f, "# generated by `autocomplete man %s`; edit freely\n", argv[i]);
            yaml_write(f, n);
            fclose(f);
            puts(path);
        }
        yaml_free(n);
    }
    return rc;
}

static void print_bash(const char *self) {
    printf(
        "_autocomplete() {\n"
        "  local line=\"${COMP_LINE:0:${COMP_POINT}}\" l\n"
        "  COMPREPLY=()\n"
        "  while IFS= read -r l; do\n"
        "    COMPREPLY[${#COMPREPLY[@]}]=\"$l\"\n"
        "  done < <(\"%s\" _complete \"$line\" \"$COMP_WORDBREAKS\" 2>/dev/null)\n"
        "}\n",
        self);
    each_spec(print_complete);
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
    if (argc >= 3 && !strcmp(argv[1], "_complete")) {
        int rc = do_complete(argv[2], argc >= 4 ? argv[3] : "\"'@><=;|&(:");
        flush();
        return rc;
    }
    if (argc >= 3 && !strcmp(argv[1], "check")) {
        char err[256];
        ynode *n = yaml_load(argv[2], err, sizeof err);
        if (!n) {
            fprintf(stderr, "%s: %s\n", argv[2], err);
            return 1;
        }
        dump(n, 0);
        putchar('\n');
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "cache")) {
        int i;
        if (argc == 2) each_spec(cache_spec);
        for (i = 2; i < argc; i++) cache_spec(argv[i]);
        return g_cache_failed;
    }
    if (argc >= 3 && !strcmp(argv[1], "man")) return man_cmds(argc - 2, argv + 2);
    if (argc >= 2 && !strcmp(argv[1], "list")) {
        each_spec(print_name);
        return 0;
    }
    fprintf(stderr,
            "usage: autocomplete bash              print bash init script\n"
            "       autocomplete list              list available specs\n"
            "       autocomplete check FILE        validate a spec, print it as JSON\n"
            "       autocomplete cache [NAME...]   precompile big specs (> 100 KB) into the cache\n"
            "       autocomplete man [-f] CMD...   write ~/.config/autocomplete/CMD.yaml from man CMD\n"
            "       autocomplete _complete LINE [WORDBREAKS]\n");
    return 2;
}
