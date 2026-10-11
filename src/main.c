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
        fputs("complete -o nospace -o bashdefault -C '", stdout);
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

/* `autocomplete list`: sorted; in columns (down, then across, like ls) on a
 * tty, one per line otherwise (exec completers read it). */
static void print_names(void) {
    int i, r, c, w = 0, cols, rows;
    qsort(g_list, g_nlist, sizeof *g_list, cmp_str);
    if (!isatty(1)) {
        for (i = 0; i < g_nlist; i++) puts(g_list[i]);
        return;
    }
    for (i = 0; i < g_nlist; i++)
        if ((int)strlen(g_list[i]) > w) w = strlen(g_list[i]);
    w += 2;
    cols = term_width() / w;
    if (cols < 1) cols = 1;
    rows = (g_nlist + cols - 1) / cols;
    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols && (i = c * rows + r) < g_nlist; c++)
            printf("%-*s", c + 1 < cols && i + rows < g_nlist ? w : 0, g_list[i]);
        putchar('\n');
    }
}
/* "NAME<TAB>PATH" of the file lookup picks; " -> TARGET" for symlinks (aliases) */
static void print_path(const char *name) {
    char path[PATH_MAX], target[PATH_MAX];
    ssize_t len;
    if (!find_spec(name, path, sizeof path)) return;
    len = readlink(path, target, sizeof target - 1);
    if (len >= 0) {
        target[len] = 0;
        printf("%s\t%s -> %s\n", name, path, target);
    } else
        printf("%s\t%s\n", name, path);
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

/* `autocomplete from man|help|cobra [--force] CMD...`: write
 * ~/.config/autocomplete/CMD.yaml from the man page, --help output, or as a
 * bridge to the program's own cobra completion. */
static int from_cmds(const char *src, int argc, char **argv) {
    ynode *(*gen)(const char *) = !strcmp(src, "man") ? man_spec : !strcmp(src, "help") ? help_spec
                                : !strcmp(src, "cobra") ? cobra_spec : NULL;
    const char *fail = !strcmp(src, "man") ? "no man page with options"
                     : !strcmp(src, "help") ? "no options or commands in --help output"
                                            : "no cobra completion (__complete)";
    char dir[PATH_MAX], path[PATH_MAX];
    int i, force = 0, rc = 0, made = 0;
    if (!gen) {
        fprintf(stderr, "from: unknown source %s (man, help, cobra)\n", src);
        return 2;
    }
    if (!getenv("HOME")) {
        fprintf(stderr, "from: HOME is not set\n");
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
        if (!(n = gen(argv[i]))) {
            fprintf(stderr, "%s: %s\n", argv[i], fail);
            rc = 1;
            continue;
        }
        if (!(f = fopen(path, "w"))) {
            perror(path);
            rc = 1;
        } else {
            fprintf(f, "# generated by `autocomplete from %s %s`; edit freely\n", src, argv[i]);
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
    if (!wb) wb = "\"'@><=;|&(:";
    /* `$VAR` word: print nothing, bash's own variable completion takes over
       (-o bashdefault) and sees unexported shell variables too */
    for (i = strlen(buf); i > 0 && !isspace((unsigned char)buf[i - 1]) && !strchr(wb, buf[i - 1]); i--) {}
    if (buf[i] == '$') { free(buf); return 0; }
    /* `user@host` word: same for bash's hostname completion; a bare `@x` or `--flag=@x`
       stays ours (spec examples) */
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
    if (argc >= 4 && !strcmp(argv[1], "from")) return from_cmds(argv[2], argc - 3, argv + 3);
    if (argc >= 2 && !strcmp(argv[1], "list")) {
        if (argc >= 3 && !strcmp(argv[2], "--paths")) each_spec(print_path);
        else { each_spec(add_name); print_names(); }
        return 0;
    }
    fprintf(stderr,
            "usage: autocomplete bash              print bash init script\n"
            "       autocomplete list [--paths]    list available specs (with the file each resolves to)\n"
            "       autocomplete check FILE        validate a spec, print it as JSON\n"
            "       autocomplete cache [NAME...]   precompile big specs (> 100 KB) into the cache\n"
            "       autocomplete from man [--force] CMD...   write ~/.config/autocomplete/CMD.yaml from man CMD\n"
            "       autocomplete from help [--force] CMD...  ... from CMD --help\n"
            "       autocomplete from cobra [--force] CMD... ... delegating to CMD's cobra completion\n"
            "       autocomplete alias [--force] CMD NAME...  complete NAME like CMD (symlinks its spec)\n"
            "       autocomplete alias --rm NAME...    remove those aliases\n"
            "       autocomplete alias --list          list aliases (NAME -> CMD)\n"
            "       autocomplete _complete LINE [WORDBREAKS]\n");
    return 2;
}
