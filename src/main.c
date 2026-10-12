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
 * `autocomplete add CMD` writes a spec for CMD (cobra, man page or --help).
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
#include <sys/stat.h>
#include <unistd.h>
#include "complete.h"
#include "manparse.h"
#include "output.h"
#include "spec.h"
#include "util.h"
#include "yaml.h"

static int g_ncomplete;
static const char *g_self;
static void print_complete(const char *name) {
    const char *p;
    if (!g_ncomplete++) {
        /* -C string is eval'd by bash: backslash-escape the path inside it,
           then single-quote the whole string for the `complete` line */
        /* $(compgen -v): the -C string runs in a subshell of the user's shell,
           so unexported variables reach `${NAME` completion too */
        fputs("complete -o nospace -o bashdefault -C 'AUTOCOMPLETE_VARS=$(compgen -v) ", stdout);
        for (p = g_self; *p; p++) {
            if (!isalnum((unsigned char)*p) && !strchr("/._-+,", *p)) putchar('\\');
            if (*p == '\'') fputs("'\\''", stdout);
            else putchar(*p);
        }
        fputs(" _complete'", stdout);
    }
    printf(" %s", name);
}
static char *g_list[2048];
static int g_nlist;
static void add_name(const char *name) { if (g_nlist < 2048) g_list[g_nlist++] = (char *)name; }

/* `list --user`: only specs resolving to a file under ~/.config/autocomplete */
static int g_list_user, g_list_paths;
static void list_spec(const char *name) {
    char path[PATH_MAX], dir[PATH_MAX];
    if (g_list_user) {
        snprintf(dir, sizeof dir, "%s/.config/autocomplete/", getenv("HOME") ? getenv("HOME") : "");
        if (!find_spec(name, path, sizeof path) || strncmp(path, dir, strlen(dir))) return;
    }
    add_name(name);
}

/* `autocomplete list`: sorted "name  description" lines; with --paths
 * " [path]" (" -> target" for symlinks, i.e. aliases) */
static void print_list(void) {
    char path[PATH_MAX], target[PATH_MAX];
    int i, w = 0;
    qsort(g_list, g_nlist, sizeof *g_list, cmp_str);
    for (i = 0; i < g_nlist; i++)
        if ((int)strlen(g_list[i]) > w) w = (int)strlen(g_list[i]);
    for (i = 0; i < g_nlist; i++) {
        ynode *n = load_spec(g_list[i]);
        const char *d = n ? spec_desc(n) : NULL;
        if (g_list_paths && find_spec(g_list[i], path, sizeof path)) {
            ssize_t len = readlink(path, target, sizeof target - 1);
            printf("%-*s  %s%s[%s", w, g_list[i], d ? d : "", d ? "  " : "", path);
            if (len >= 0) printf(" -> %.*s", (int)len, target);
            putchar(']');
        } else if (d) printf("%-*s  %s", w, g_list[i], d);
        else printf("%s", g_list[i]);
        putchar('\n');
        if (n) yaml_free(n);
    }
}

static int g_cache_failed;

/* `autocomplete cache`: compile a big spec (> CACHE_MIN) unconditionally. */
static void cache_spec(const char *name) {
    char path[PATH_MAX], bin[PATH_MAX], err[256];
    struct stat st;
    ynode *n;
    if (!find_spec(name, path, sizeof path)) {
        /* listed but unreadable: a dangling symlink (e.g. a stale alias); warn only */
        fprintf(stderr, "%s: no such spec (broken symlink? fix: autocomplete alias --force CMD %s)\n", name, name);
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

/* `autocomplete add [--force] [--from man|help|cobra] CMD...`: write
 * ~/.config/autocomplete/CMD.yaml from the first source that works (cobra
 * bridge, man page, --help output), or only from --from SRC. */
static const struct {
    const char *name;
    ynode *(*gen)(const char *);
    const char *fail;
} sources[] = {
    {"cobra", cobra_spec, "no cobra completion (__complete)"},
    {"man", man_spec, "no man page with options"},
    {"help", help_spec, "no options or commands in --help output"},
};
#define NSOURCES (int)(sizeof sources / sizeof *sources)

static int add_cmds(int argc, char **argv) {
    char dir[PATH_MAX], path[PATH_MAX];
    int i, s, from = -1, force = 0, rc = 0, made = 0;
    for (i = 0; i < argc; i++) {
        const char *src = NULL;
        if (!strcmp(argv[i], "--force")) force = 1;
        else if (!strcmp(argv[i], "--from") && i + 1 < argc) src = argv[++i];
        else if (!strncmp(argv[i], "--from=", 7)) src = argv[i] + 7;
        else if (!strcmp(argv[i], "--from")) {
            fprintf(stderr, "add: --from needs a source (cobra, man, help)\n");
            return 2;
        }
        else if (argv[i][0] == '-') {
            fprintf(stderr, "add: unknown option %s\n", argv[i]);
            return 2;
        }
        if (!src) continue;
        for (from = 0; from < NSOURCES && strcmp(sources[from].name, src); from++) ;
        if (from == NSOURCES) {
            fprintf(stderr, "add: unknown source %s (cobra, man, help)\n", src);
            return 2;
        }
    }
    if (!getenv("HOME")) {
        fprintf(stderr, "add: HOME is not set\n");
        return 1;
    }
    snprintf(dir, sizeof dir, "%s/.config/autocomplete", getenv("HOME"));
    mkdirs(dir);
    for (i = 0; i < argc; i++) {
        ynode *n = NULL, *g, **at;
        FILE *f;
        int use = from;
        if (!strcmp(argv[i], "--from")) {
            i++;
            continue;
        }
        if (argv[i][0] == '-') continue;
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
        /* --force without --from: regenerate from the source recorded in `generated` */
        if (use < 0 && (g = yaml_load(path, NULL, 0))) {
            const ynode *k = yaml_get(g, "generated");
            for (s = 0; k && k->type == Y_STR && s < NSOURCES; s++)
                if (!strcmp(sources[s].name, k->str)) use = s;
            yaml_free(g);
        }
        for (s = use < 0 ? 0 : use; s < (use < 0 ? NSOURCES : use + 1); s++)
            if ((n = sources[s].gen(argv[i]))) break;
        if (!n) {
            fprintf(stderr, "%s: %s\n", argv[i],
                    use < 0 ? "no cobra completion, man page or --help options" : sources[use].fail);
            rc = 1;
            continue;
        }
        /* `generated: SRC` right after `description` (set_desc writes it first) */
        g = yaml_new(NULL, Y_STR, "generated", sources[s].name);
        at = n->child && !strcmp(n->child->key, "description") ? &n->child->next : &n->child;
        g->next = *at;
        *at = g;
        if (!(f = fopen(path, "w"))) {
            perror(path);
            rc = 1;
        } else {
            fputs("# generated by autocomplete add; edit freely\n", f);
            yaml_write(f, n);
            fclose(f);
            printf("%s (%s)\n", path, sources[s].name);
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

/* `${NAME<op>` cheat-sheet (bash 3.2 forms only). `op`: what is typed after
   the name (`pre`: right after `${`; `0` stands for any digit), `ex`: the
   example after the name (`pre`: the whole expansion), `res`: its result when
   the variable holds PARAM_SAMPLE. */
#define PARAM_SAMPLE "src/lib/a.tar.gz"
static const struct { int pre; const char *op, *ex, *res, *desc; } params[] = {
    {0, "}", "}", PARAM_SAMPLE, "sample value for the rows below"},
    {0, "%", "%.*}", "src/lib/a.tar", "cut shortest matching suffix"},
    {0, "%%", "%%.*}", "src/lib/a", "cut longest matching suffix"},
    {0, "%/*", "%/*}", "src/lib", "dirname"},
    {0, "#", "#*/}", "lib/a.tar.gz", "cut shortest matching prefix"},
    {0, "##", "##*/}", "a.tar.gz", "cut longest matching prefix: basename"},
    {0, "##*.", "##*.}", "gz", "extension"},
    {0, "/", "/a/X}", "src/lib/X.tar.gz", "replace first match"},
    {0, "//", "//a/X}", "src/lib/X.tXr.gz", "replace every match"},
    {0, "/#", "/#src/out}", "out/lib/a.tar.gz", "replace at start"},
    {0, "/%", "/%gz/xz}", "src/lib/a.tar.xz", "replace at end"},
    {0, ":-", ":-dflt}", "dflt if unset/empty", "default"},
    {0, "-", "-dflt}", "dflt if unset", "default, empty stays"},
    {0, ":=", ":=dflt}", "dflt if unset/empty", "default, also assigned"},
    {0, ":+", ":+alt}", "alt if non-empty", "alternate, else empty"},
    {0, ":?", ":?msg}", "error if unset/empty", "required, msg on error"},
    {0, ":0", ":4}", "lib/a.tar.gz", "substring from offset"},
    {0, ":0:", ":4:3}", "lib", "substring: offset, length"},
    {0, ": -", ": -6}", "tar.gz", "last N chars (space required)"},
    {1, "#", "${#v}", "16", "length"},
    {1, "!*", "${!PA*}", "PATH PAGER ...", "names starting with PA"},
    {1, "!", "${!ref}", "$NAME if ref=NAME", "indirection"},
};

/* Word `${…` with an operator started: list the matching forms display-only
   (emoji first, so bash lists them and never inserts one). 0: not ours. */
static int complete_param(const char *w) {
    const char *p = w + 2, *vars = getenv("AUTOCOMPLETE_VARS");
    char name[64], desc[256];
    size_t i, k, ol, nl, want, rw;
    char *ex[sizeof params / sizeof *params] = {0}, show[sizeof params / sizeof *params] = {0};
    int n = 0, exact = 0, pre = *p == '#' || *p == '!';
    if (!pre) {
        if (isalpha((unsigned char)*p) || *p == '_')
            while (isalnum((unsigned char)*p) || *p == '_') p++;
        else if (isdigit((unsigned char)*p) || *p == '@' || *p == '*') p++;
        else if (*p) return 0;
    }
    ol = strlen(p);
    if (strchr(p, '}') || (pre && ol > 1)) return 0;
    g_cur = w;
    g_kind = NULL;
    g_verbatim = 1;
    if (!ol && !pre) {
        /* `${NAME`: shell variable names (`compgen -v` from the init line) as
           `${NAME` without the `}` bash would add; an exact name also lists the
           operators. No list (older init line): bash completes the name. */
        const char *v, *e;
        if (!vars) { g_verbatim = 0; return 0; }
        nl = p - w;
        for (v = vars; *v; v = *e ? e + 1 : e) {
            e = strchr(v, '\n');
            if (!e) e = v + strlen(v);
            if ((size_t)(e - v) + 2 < nl || (size_t)(e - v) + 3 > sizeof name || strncmp(v, w + 2, nl - 2)) continue;
            snprintf(name, sizeof name, "${%.*s", (int)(e - v), v);
            if (!strcmp(name, w)) exact = 1;
            else emit_desc(name, 0, NULL);
        }
        if (!exact) { g_verbatim = 0; return ncands() > 0; }
    }
    g_cur = w;
    g_kind = NULL;
    g_verbatim = 1;
    for (i = 0, want = rw = 0; i < sizeof params / sizeof *params; i++) {
        const char *o = params[i].op;
        if (params[i].pre != pre) continue;
        /* typed text must be a prefix of the form; any digit matches the 0 placeholder */
        for (k = 0; k < ol && o[k]; k++)
            if (o[k] != p[k] && !(o[k] == '0' && isdigit((unsigned char)p[k]))) break;
        if (k < ol) continue;
        show[i] = 1;
        ex[i] = pre ? strdup(params[i].ex) : NULL;
        if (!pre && asprintf(&ex[i], "%.*s%s", (int)(p - w), w, params[i].ex) < 0) ex[i] = NULL;
        if (ex[i] && strlen(ex[i]) > want) want = strlen(ex[i]);
        if (strlen(params[i].res) > rw) rw = strlen(params[i].res);
    }
    for (i = 0; i < sizeof params / sizeof *params; i++) {
        if (!show[i] || !ex[i]) continue;
        snprintf(name, sizeof name, "%s\xc2\xa0%s", n ? "🔥" : "📌", params[i].op);
        snprintf(desc, sizeof desc, "%-*s → %-*s 📚 %s", (int)want, ex[i], (int)rw, params[i].res, params[i].desc);
        emit_desc(name, 0, desc);
        free(ex[i]);
        n++;
    }
    if (ncands() == 1) emit_desc("🔎\xc2\xa0keep", 0, "typed text stays"); /* a lone entry would be inserted */
    g_verbatim = 0;
    return 1;
}

/* `complete -C` passes the line in $COMP_LINE, the cursor (in characters) in
   $COMP_POINT; COMP_WORDBREAKS isn't exported, so it is used only if the user
   exported it. */
static int complete_env(void) {
    const char *line = getenv("COMP_LINE"), *pt = getenv("COMP_POINT");
    const char *wb = getenv("COMP_WORDBREAKS");
    char *buf;
    size_t i = 0, j;
    char *k;
    long n;
    int rc;
    if (!line) return 1;
    buf = strdup(line);
    if (pt) {
        for (n = atol(pt); n > 0 && buf[i]; n--)
            for (i++; (buf[i] & 0xC0) == 0x80; i++) {}
        buf[i] = 0;
    }
    if (!wb) wb = "\"'@><=;|&(:";
    /* `$VAR` word: print nothing, bash's own variable completion takes over
       (-o bashdefault) and sees unexported shell variables too */
    for (i = strlen(buf); i > 0 && !isspace((unsigned char)buf[i - 1]) && !strchr(wb, buf[i - 1]); i--) {}
    /* `${NAME<op>`: our cheat-sheet; `:` is a wordbreak, so take the last `${`
       of the space/quote-delimited word */
    for (j = strlen(buf); j > 0 && !strchr(" \t\"'", buf[j - 1]); j--) {}
    for (k = buf + strlen(buf); k > buf + j && strncmp(k, "${", 2); k--) {}
    if (!strncmp(k, "${", 2) && complete_param(k)) {
        flush();
        free(buf);
        return 0;
    }
    if (buf[i] == '$') { free(buf); return 0; }
    /* `user@host` word: same for bash's hostname completion; a bare `@x` or `--flag=@x`
       stays ours (spec snippets) */
    if (i >= 2 && buf[i - 1] == '@' && !isspace((unsigned char)buf[i - 2]) && buf[i - 2] != '=') { free(buf); return 0; }
    rc = do_complete(buf, wb);
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
    if (argc >= 2 && argc <= 3 && !strcmp(argv[1], "emoji")) return emoji_cmd(argv[2]);
    if (argc >= 3 && !strcmp(argv[1], "add")) return add_cmds(argc - 2, argv + 2);
    if (argc >= 2 && !strcmp(argv[1], "list")) {
        int i;
        for (i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "--paths")) g_list_paths = 1;
            else if (!strcmp(argv[i], "--user")) g_list_user = 1;
            else {
                fprintf(stderr, "usage: autocomplete list [--user] [--paths]\n");
                return 2;
            }
        }
        each_spec(list_spec);
        print_list();
        return 0;
    }
    fprintf(stderr,
            "usage: autocomplete bash              print bash init script\n"
            "       autocomplete list [--user] [--paths]  list specs with descriptions (--user: only\n"
            "                                  ~/.config/autocomplete; --paths: with the spec file)\n"
            "       autocomplete check FILE        validate a spec, print it as JSON\n"
            "       autocomplete cache [NAME...]   precompile big specs (> 100 KB) into the cache\n"
            "       autocomplete add [--force] [--from cobra|man|help] CMD...  write\n"
            "                                  ~/.config/autocomplete/CMD.yaml from the first source that works\n"
            "       autocomplete alias [--force] CMD NAME...  complete NAME like CMD (symlinks its spec)\n"
            "       autocomplete alias --rm NAME...    remove those aliases\n"
            "       autocomplete alias --list          list aliases (NAME -> CMD)\n"
            "       autocomplete emoji [NAME|EMOJI]   snippet mark names (NAME -> emoji, emoji -> NAME)\n"
            "       autocomplete _complete LINE [WORDBREAKS]\n");
    return 2;
}
