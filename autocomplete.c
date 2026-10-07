/*
 * autocomplete - tiny carapace-like completion engine for bash 3.2 (macOS).
 *
 *   eval "$(autocomplete bash)"          # register completions for all specs
 *   autocomplete _complete "$COMP_LINE_UP_TO_POINT" "$COMP_WORDBREAKS"
 *
 * Commands are described by JSON specs (<command>.json), looked up in:
 *   $AUTOCOMPLETE_SPECS (colon list), ~/.config/autocomplete,
 *   <binary dir>/specs, <binary dir>/../share/autocomplete
 *
 * Output of _complete: one candidate per line, already shell-escaped and
 * already trimmed to bash's notion of the current word (COMP_WORDBREAKS).
 * Candidates that are "finished" end with a space; bash 3.2 has no compopt,
 * so the function is registered with -o nospace and we add spaces ourselves.
 */
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "json.h"

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

static jnode *load_spec(const char *name) {
    char path[PATH_MAX], err[256];
    int i;
    if (strchr(name, '/') || name[0] == '.') return NULL;
    for (i = 0; i < g_ndirs; i++) {
        jnode *n;
        snprintf(path, sizeof path, "%s/%s.json", g_dirs[i], name);
        if (access(path, R_OK) != 0) continue;
        n = json_load(path, err, sizeof err);
        if (!n && getenv("AUTOCOMPLETE_DEBUG")) fprintf(stderr, "autocomplete: %s: %s\n", path, err);
        return n;
    }
    return NULL;
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

/* True if cand was already printed (union values can overlap). */
static int seen_before(const char *cand) {
    static char *printed[4096];
    static int np;
    int i;
    for (i = 0; i < np; i++)
        if (!strcmp(printed[i], cand)) return 1;
    if (np < 4096) printed[np++] = strdup(cand);
    return 0;
}

/* Escape for bash and print, trimmed to bash's current word. */
static void emit(const char *cand, int space) {
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
    if (seen_before(p)) return;
    printf("%s%s\n", p, space ? " " : "");
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

/* Emit explicit targets of a makefile: "name [name...]: deps", skipping
 * variable assignments, recipes, special (.PHONY) and pattern (%) rules. */
static void complete_make_targets(void) {
    static const char *defaults[] = {"GNUmakefile", "makefile", "Makefile", 0};
    char path[4096], line[4096];
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
    if (!f) return;
    while (fgets(line, sizeof line, f)) {
        char *colon = strchr(line, ':'), *tok;
        if (line[0] == '\t' || line[0] == '#' || !colon) continue;
        if (colon[1] == '=' || (colon[1] == ':' && colon[2] == '=')) continue;
        *colon = 0;
        if (strpbrk(line, "=$%")) continue;
        for (tok = strtok(line, " \t"); tok; tok = strtok(NULL, " \t"))
            if (tok[0] != '.') emit(tok, 1);
    }
    fclose(f);
}

/* Complete a <value> from a spec: null, "any"/"file"/"dir", "@builtin",
 * {"exec": ..., "suffix": ..., "nospace": ...}, or an array (union). */
static void complete_value(const jnode *v, const jnode *parent) {
    if (!v) return;
    switch (v->type) {
    case J_ARR: {
        const jnode *e;
        for (e = v->child; e; e = e->next) complete_value(e, parent);
        break;
    }
    case J_STR:
        if (!strcmp(v->str, "file")) complete_files(0);
        else if (!strcmp(v->str, "dir")) complete_files(1);
        else if (!strcmp(v->str, "@make-targets")) complete_make_targets();
        else if (!strcmp(v->str, "@subcommands")) {
            const jnode *c;
            for (c = json_get(parent, "commands") ? json_get(parent, "commands")->child : NULL; c; c = c->next)
                emit(c->key, 1);
        }
        break;
    case J_OBJ: {
        const jnode *ex = json_get(v, "exec"), *sx = json_get(v, "suffix"), *ns = json_get(v, "nospace");
        char shell[4096];
        if (!ex || ex->type != J_STR) break;
        snprintf(shell, sizeof shell, "%s 2>/dev/null", ex->str);
        emit_cmd_output(shell, sx && sx->type == J_STR ? sx->str : NULL,
                        !(ns && ns->type == J_BOOL && ns->num));
        break;
    }
    default: break;
    }
}

/* Find flag `name` in cmd's flags; *val receives its value spec. */
static int find_flag(const jnode *cmd, const char *name, const jnode **val) {
    const jnode *f = json_get(json_get(cmd, "flags"), name);
    if (f) *val = f;
    return f != NULL;
}

static int takes_value(const jnode *v) { return v && v->type != J_NULL; }

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
    const jnode *root, *c, *parent = NULL, *v = NULL, *subs;
    const char *cur, *slash;

    g_wbreaks = wbreaks;
    g_raw = line + raw;
    cur = g_cur = w[n - 1];

    slash = strrchr(w[0], '/');
    if (!(root = c = load_spec(slash ? slash + 1 : w[0]))) return 0;

    /* walk the already-typed words */
    for (i = 1; i < n - 1; i++) {
        const jnode *s;
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
        if ((s = json_get(json_get(c, "commands"), w[i]))) { parent = c; c = s; }
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
        const jnode *f;
        for (f = json_get(c, "flags") ? json_get(c, "flags")->child : NULL; f; f = f->next)
            emit(f->key, 1);
        return 0;
    }

    subs = json_get(c, "commands");
    for (v = subs ? subs->child : NULL; v; v = v->next) emit(v->key, 1);
    v = json_get(c, "args");
    /* "@subcommands" on a leaf (docker help) lists the parent's commands */
    complete_value(v, v && v->type == J_STR && !strcmp(v->str, "@subcommands") && parent ? parent : c);
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
            size_t len = strlen(e->d_name);
            if (len <= 5 || e->d_name[0] == '.' || strcmp(e->d_name + len - 5, ".json")) continue;
            e->d_name[len - 5] = 0;
            for (j = 0; j < nn && strcmp(names[j], e->d_name); j++) {}
            if (j == nn) names[nn++] = strdup(e->d_name);
        }
        closedir(d);
    }
    for (i = 0; i < nn; i++) fn(names[i]);
}

static void print_complete(const char *name) { printf("complete -o nospace -F _autocomplete %s\n", name); }
static void print_name(const char *name) { puts(name); }

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

int main(int argc, char **argv) {
    init_dirs();
    if (argc >= 2 && !strcmp(argv[1], "bash")) {
        char abs[PATH_MAX];
        /* bare name -> found via PATH at completion time; otherwise pin it */
        print_bash(strchr(argv[0], '/') && realpath(argv[0], abs) ? abs : argv[0]);
        return 0;
    }
    if (argc >= 3 && !strcmp(argv[1], "_complete"))
        return do_complete(argv[2], argc >= 4 ? argv[3] : "\"'@><=;|&(:");
    if (argc >= 2 && !strcmp(argv[1], "list")) {
        each_spec(print_name);
        return 0;
    }
    fprintf(stderr,
            "usage: autocomplete bash              print bash init script\n"
            "       autocomplete list              list available specs\n"
            "       autocomplete _complete LINE [WORDBREAKS]\n");
    return 2;
}
