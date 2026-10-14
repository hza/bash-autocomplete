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
/* `complete … -C '<self> _complete'` without the names */
static void print_complete_cmd(void) {
    const char *p;
    {
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
}
static void print_complete(const char *name) {
    if (!g_ncomplete++) print_complete_cmd();
    printf(" %s", name);
}
static char *g_list[2048];
static int g_nlist;
static void add_name(const char *name) { if (g_nlist < 2048) g_list[g_nlist++] = (char *)name; }

/* `list --user`: only specs resolving to a file under ~/.config/autocomplete */
static int g_list_user, g_list_paths, g_list_tsv;
static void list_spec(const char *name) {
    char path[PATH_MAX], dir[PATH_MAX];
    if (g_list_user) {
        snprintf(dir, sizeof dir, "%s/.config/autocomplete/", getenv("HOME") ? getenv("HOME") : "");
        if (!find_spec(name, path, sizeof path) || strncmp(path, dir, strlen(dir))) return;
    }
    add_name(name);
}

/* `version` of the spec file at path ("" if none, NULL if unreadable). */
static char *spec_version(const char *path) {
    ynode *n = yaml_load(path, NULL, 0);
    const ynode *v;
    char *r;
    if (!n) return NULL;
    v = yaml_get(n, "version");
    r = strdup(v && v->type == Y_STR ? v->str : "");
    yaml_free(n);
    return r;
}

/* User copy of `name` older than the shipped spec: fills bundled path and
 * both versions (caller frees), returns 1. */
static int outdated(const char *name, const char *user, char *bpath, size_t size, char **bv, char **uv) {
    *bv = *uv = NULL;
    if (!find_bundled(name, bpath, size) || !(*bv = spec_version(bpath)) || !**bv) goto no;
    if (!(*uv = spec_version(user))) goto no;
    if (strcmp(*bv, *uv) > 0) return 1;
no:
    free(*bv);
    free(*uv);
    *bv = *uv = NULL;
    return 0;
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
        char bpath[PATH_MAX], *bv, *uv, user[PATH_MAX], upd[128] = "";
        if (g_list_tsv) {
            printf("%s\t%s\n", g_list[i], d ? d : "");
            if (n) yaml_free(n);
            continue;
        }
        snprintf(user, sizeof user, "%s/.config/autocomplete/", getenv("HOME") ? getenv("HOME") : "");
        if (find_spec(g_list[i], path, sizeof path) && !strncmp(path, user, strlen(user)) &&
            outdated(g_list[i], path, bpath, sizeof bpath, &bv, &uv)) {
            snprintf(upd, sizeof upd, "  (update: %s > %s)", bv, *uv ? uv : "none");
            free(bv);
            free(uv);
        }
        if (g_list_paths && find_spec(g_list[i], path, sizeof path)) {
            ssize_t len = readlink(path, target, sizeof target - 1);
            printf("%-*s  %s%s[%s", w, g_list[i], d ? d : "", d ? "  " : "", path);
            if (len >= 0) printf(" -> %.*s", (int)len, target);
            putchar(']');
        } else if (d) printf("%-*s  %s", w, g_list[i], d);
        else printf("%s", g_list[i]);
        printf("%s\n", upd);
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
    printf("\nTo use it now, run (or open a new shell):\n\n    eval \"$(autocomplete bash)\"\n\n");
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

/* Do the two files have the same bytes? */
static int same_file(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    int ca, cb, r = 0;
    if (fa && fb) {
        do { ca = getc(fa); cb = getc(fb); } while (ca == cb && ca != EOF);
        r = ca == cb;
    }
    if (fa) fclose(fa);
    if (fb) fclose(fb);
    return r;
}

static int copy_file(const char *src, const char *dst) {
    char buf[8192];
    size_t len;
    FILE *in = fopen(src, "rb"), *out;
    if (!in) return perror(src), 0;
    if (!(out = fopen(dst, "wb"))) return perror(dst), fclose(in), 0;
    while ((len = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, len, out);
    fclose(in);
    return fclose(out) == 0;
}

/* `autocomplete edit NAME`: open ~/.config/autocomplete/NAME.yaml in
 * $VISUAL/$EDITOR/vi, copying the shipped spec there first if needed. */
static int edit_cmd(const char *name) {
    char dir[PATH_MAX], path[PATH_MAX], real[PATH_MAX], src[PATH_MAX], err[256], cmd[PATH_MAX * 2 + 64], *bv, *uv, *q;
    const char *ed = getenv("VISUAL");
    struct stat st;
    ynode *n;
    int rc;
    if (!valid_name(name)) return fprintf(stderr, "%s: not a command name\n", name), 1;
    if (!getenv("HOME")) return fprintf(stderr, "edit: HOME is not set\n"), 1;
    snprintf(dir, sizeof dir, "%s/.config/autocomplete", getenv("HOME"));
    snprintf(path, sizeof path, "%s/%s.yaml", dir, name);
    if (access(path, F_OK) != 0) snprintf(path, sizeof path, "%s/%s.yml", dir, name);
    if (access(path, F_OK) == 0) {
        if (!realpath(path, real)) return perror(path), 1;
        if (strcmp(real, path)) printf("%s is an alias of %s\n", path, real);
        if (outdated(name, real, src, sizeof src, &bv, &uv)) {
            fprintf(stderr, "%s: shipped version %s is newer than your copy (%s); see: diff %s %s\n",
                    name, bv, *uv ? uv : "none", src, real);
            free(bv);
            free(uv);
        }
    } else {
        if (!find_bundled(name, src, sizeof src))
            return fprintf(stderr, "%s: no spec (create one: autocomplete add %s)\n", name, name), 1;
        snprintf(real, sizeof real, "%s/%s.yaml", dir, name);
        mkdirs(dir);
        if (!copy_file(src, real)) return 1;
        printf("copied %s -> %s\n", src, real);
    }
    if (!ed || !*ed) ed = getenv("EDITOR");
    if (!ed || !*ed) ed = "vi";
    /* editor may carry args (`code -w`): run via sh, path single-quoted */
    rc = snprintf(cmd, sizeof cmd, "%s '", ed);
    for (q = real; *q && rc < (int)sizeof cmd - 8; q++)
        rc += *q == '\'' ? snprintf(cmd + rc, sizeof cmd - rc, "'\\''") : snprintf(cmd + rc, sizeof cmd - rc, "%c", *q);
    snprintf(cmd + rc, sizeof cmd - rc, "'");
    if (system(cmd) != 0) fprintf(stderr, "edit: '%s' exited with an error\n", ed);
    /* unchanged copy of the shipped spec: drop it, the shipped one is used */
    if (lstat(real, &st) == 0 && !S_ISLNK(st.st_mode) && find_bundled(name, src, sizeof src) &&
        strcmp(src, real) && same_file(real, src)) {
        if (unlink(real) == 0) printf("no changes; removed %s (the shipped spec is used)\n", real);
        return 0;
    }
    if (!(n = yaml_load(real, err, sizeof err))) return fprintf(stderr, "%s: %s\n", real, err), 1;
    rc = check_groups(n, n, real);
    if (stat(real, &st) == 0 && st.st_size > CACHE_MIN && !save_cache(name, n, real, src, sizeof src))
        fprintf(stderr, "%s: cannot write cache\n", name);
    yaml_free(n);
    return rc;
}

/* `autocomplete updates [NAME]`: your copies in ~/.config/autocomplete
 * (aliases skipped) whose shipped spec has a newer version; with NAME,
 * diff -u yours against the shipped one. Exit 1 if any update. */
static int updates_cmd(const char *name) {
    char user[PATH_MAX], path[PATH_MAX], bpath[PATH_MAX], *bv, *uv;
    struct stat st;
    int i, w = 0, found = 0;
    if (name) {
        if (!valid_name(name)) return fprintf(stderr, "%s: not a command name\n", name), 1;
        if (!find_spec(name, path, sizeof path) || !outdated(name, path, bpath, sizeof bpath, &bv, &uv))
            return fprintf(stderr, "%s: no update for your copy\n", name), 1;
        fflush(stdout);
        execlp("diff", "diff", "-u", path, bpath, (char *)NULL);
        return perror("diff"), 1;
    }
    snprintf(user, sizeof user, "%s/.config/autocomplete/", getenv("HOME") ? getenv("HOME") : "");
    g_list_user = 1;
    each_spec(list_spec);
    qsort(g_list, g_nlist, sizeof *g_list, cmp_str);
    for (i = 0; i < g_nlist; i++)
        if ((int)strlen(g_list[i]) > w) w = (int)strlen(g_list[i]);
    for (i = 0; i < g_nlist; i++) {
        if (!find_spec(g_list[i], path, sizeof path) || lstat(path, &st) || S_ISLNK(st.st_mode)) continue;
        if (find_bundled(g_list[i], bpath, sizeof bpath) && same_file(path, bpath)) {
            printf("%-*s  same as shipped, not needed: rm %s\n", w, g_list[i], path);
            found |= 1;
            continue;
        }
        if (!outdated(g_list[i], path, bpath, sizeof bpath, &bv, &uv)) continue;
        printf("%-*s  yours %s, shipped %s\n", w, g_list[i], *uv ? uv : "(no version)", bv);
        found |= 2;
        free(bv);
        free(uv);
    }
    if (found & 2) printf("see changes: autocomplete updates NAME\n");
    return found != 0;
}

/* spec names and names with a completion already (`complete -p` lines on
   stdin, name = last word): left out of the man list */
static char *g_skip[8192];
static int g_nskip;
static void add_skip(const char *name) {
    if (g_nskip < 8192) g_skip[g_nskip++] = strdup(name);
}
static void print_man_name(const char *name) {
    if (bsearch(&name, g_skip, g_nskip, sizeof *g_skip, cmp_str)) return;
    printf("%s\n", name);
}

/* `_man_names`: commands for `bash --man`, see print_bash. */
static int man_names(void) {
    char line[4096];
    while (fgets(line, sizeof line, stdin)) {
        char *e = line + strcspn(line, "\n"), *w;
        *e = 0;
        if ((w = strrchr(line, ' '))) add_skip(w + 1);
    }
    each_spec(add_skip);
    qsort(g_skip, g_nskip, sizeof *g_skip, cmp_str);
    each_man_command(print_man_name);
    return 0;
}

/* `bash --man`: also every $PATH command with a man page, except ones that
 * already have a completion (bash-completion, brew, ...): their spec is
 * generated from the man page on the first TAB (load_man_spec). The list is
 * made at shell start by `_man_names`, which reads `complete -p`. */
static void print_bash(const char *self, int man) {
    const char *p;
    g_self = self;
    each_spec(print_complete);
    if (g_ncomplete) putchar('\n');
    if (!man) return;
    fputs("_ac_man=$(complete -p | ", stdout);
    for (p = self; *p; p++) {
        if (!isalnum((unsigned char)*p) && !strchr("/._-+,", *p)) putchar('\\');
        putchar(*p);
    }
    fputs(" _man_names)\n[ -n \"$_ac_man\" ] && ", stdout);
    print_complete_cmd();
    fputs(" $_ac_man\nunset _ac_man\n", stdout);
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
    if (argc >= 2 && argc <= 3 && !strcmp(argv[1], "bash")) {
        char abs[PATH_MAX];
        int man = argc == 3 && !strcmp(argv[2], "--man");
        if (argc == 3 && !man) {
            fprintf(stderr, "bash: unknown option %s (--man)\n", argv[2]);
            return 2;
        }
        /* bare name -> found via PATH at completion time; otherwise pin it */
        print_bash(strchr(argv[0], '/') && realpath(argv[0], abs) ? abs : argv[0], man);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "_complete")) return complete_env();
    if (argc == 2 && !strcmp(argv[1], "_man_names")) return man_names();
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
    if (argc == 3 && !strcmp(argv[1], "edit")) return edit_cmd(argv[2]);
    if (argc <= 3 && argc >= 2 && !strcmp(argv[1], "updates")) return updates_cmd(argv[2]);
    if (argc >= 2 && !strcmp(argv[1], "list")) {
        int i;
        for (i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "--paths")) g_list_paths = 1;
            else if (!strcmp(argv[i], "--user")) g_list_user = 1;
            else if (!strcmp(argv[i], "--tsv")) g_list_tsv = 1;
            else {
                fprintf(stderr, "usage: autocomplete list [--user] [--paths|--tsv]\n");
                return 2;
            }
        }
        each_spec(list_spec);
        print_list();
        return 0;
    }
    fprintf(stderr,
            "usage: autocomplete bash              print bash init script\n"
            "       autocomplete list [--user] [--paths|--tsv]  list specs with descriptions (--user: only\n"
            "                                  ~/.config/autocomplete; --paths: with the spec file;\n"
            "                                  --tsv: NAME<TAB>DESCRIPTION lines for scripts)\n"
            "       autocomplete check FILE        validate a spec, print it as JSON\n"
            "       autocomplete cache [NAME...]   precompile big specs (> 100 KB) into the cache\n"
            "       autocomplete add [--force] [--from cobra|man|help] CMD...  write\n"
            "                                  ~/.config/autocomplete/CMD.yaml from the first source that works\n"
            "       autocomplete edit NAME         open ~/.config/autocomplete/NAME.yaml in $VISUAL/$EDITOR\n"
            "                                  (copies the shipped spec there first)\n"
            "       autocomplete updates [NAME]    your edited specs with a newer shipped version\n"
            "                                  (NAME: diff -u yours shipped)\n"
            "       autocomplete alias [--force] CMD NAME...  complete NAME like CMD (symlinks its spec)\n"
            "       autocomplete alias --rm NAME...    remove those aliases\n"
            "       autocomplete alias --list          list aliases (NAME -> CMD)\n"
            "       autocomplete emoji [NAME|EMOJI]   snippet mark names (NAME -> emoji, emoji -> NAME)\n"
            "       autocomplete _complete LINE [WORDBREAKS]\n"
            "\n"
            "Manual install: add this line to ~/.bash_profile (or ~/.bashrc):\n"
            "\n"
            "    eval \"$(autocomplete bash)\"\n"
            "\n");
    return 2;
}
