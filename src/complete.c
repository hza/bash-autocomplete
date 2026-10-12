/* Spec walk: split the line, follow subcommands and flags, complete the
 * value under the cursor. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "builtins.h"
#include "complete.h"
#include "output.h"
#include "spec.h"

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
/* A snippet's `key`: one char of [0-9a-z] (typed as "@key"). */
int snippet_key(const ynode *k) {
    return k && k->type == Y_STR && k->str[0] && !k->str[1] && (isdigit((unsigned char)k->str[0]) || islower((unsigned char)k->str[0]));
}

/* A snippet's `mark` icon: a name from the table below or one emoji of a
 * single 4-byte code point in U+1F300..U+1FAFF (no U+FE0F / ZWJ sequences,
 * whose drawn width varies by terminal; these are 2 columns, as dwidth
 * counts). NULL if invalid. Table entries are East Asian Width W (2 cols
 * everywhere) except the keycaps 1..9, 1 column (dwidth): their extra
 * U+00A0 keeps "@key" aligned. 📚 🔎 🧩 are left out (engine markers). */
static const struct { const char *name, *icon; } mark_names[] = {
    {"1", "\x31\xef\xb8\x8f\xe2\x83\xa3\xc2\xa0"}, /* 1️⃣ */
    {"2", "\x32\xef\xb8\x8f\xe2\x83\xa3\xc2\xa0"}, /* 2️⃣ */
    {"3", "\x33\xef\xb8\x8f\xe2\x83\xa3\xc2\xa0"}, /* 3️⃣ */
    {"4", "\x34\xef\xb8\x8f\xe2\x83\xa3\xc2\xa0"}, /* 4️⃣ */
    {"5", "\x35\xef\xb8\x8f\xe2\x83\xa3\xc2\xa0"}, /* 5️⃣ */
    {"6", "\x36\xef\xb8\x8f\xe2\x83\xa3\xc2\xa0"}, /* 6️⃣ */
    {"7", "\x37\xef\xb8\x8f\xe2\x83\xa3\xc2\xa0"}, /* 7️⃣ */
    {"8", "\x38\xef\xb8\x8f\xe2\x83\xa3\xc2\xa0"}, /* 8️⃣ */
    {"9", "\x39\xef\xb8\x8f\xe2\x83\xa3\xc2\xa0"}, /* 9️⃣ */
    {"bell", "\xf0\x9f\x94\x94"},                  /* 🔔 */
    {"bulb", "\xf0\x9f\x92\xa1"},                  /* 💡 */
    {"calendar", "\xf0\x9f\x93\x85"},              /* 📅 */
    {"chart", "\xf0\x9f\x93\x88"},                 /* 📈 */
    {"clock", "\xf0\x9f\x95\x90"},                 /* 🕐 */
    {"eyes", "\xf0\x9f\x91\x80"},                  /* 👀 */
    {"fire", "\xf0\x9f\x94\xa5"},                  /* 🔥 */
    {"globe", "\xf0\x9f\x8c\x90"},                 /* 🌐 */
    {"key", "\xf0\x9f\x94\x91"},                   /* 🔑 */
    {"link", "\xf0\x9f\x94\x97"},                  /* 🔗 */
    {"lock", "\xf0\x9f\x94\x92"},                  /* 🔒 */
    {"memo", "\xf0\x9f\x93\x9d"},                  /* 📝 */
    {"package", "\xf0\x9f\x93\xa6"},               /* 📦 */
    {"pin", "\xf0\x9f\x93\x8c"},                   /* 📌 */
    {"pumpkin", "\xf0\x9f\x8e\x83"},               /* 🎃 */
    {"rocket", "\xf0\x9f\x9a\x80"},                /* 🚀 */
    {"siren", "\xf0\x9f\x9a\xa8"},                 /* 🚨 */
    {"skull", "\xf0\x9f\x92\x80"},                 /* 💀 */
    {"star", "\xf0\x9f\x8c\x9f"},                  /* 🌟 */
    {"target", "\xf0\x9f\x8e\xaf"},                /* 🎯 */
    {"test", "\xf0\x9f\xa7\xaa"},                  /* 🧪 */
    {"whale", "\xf0\x9f\x90\xb3"},                 /* 🐳 */
};
#define NMARKS (sizeof mark_names / sizeof *mark_names)

const char *snippet_mark(const char *m) {
    const unsigned char *u = (const unsigned char *)m;
    unsigned cp;
    size_t i;
    char d[2] = {0};
    /* a literal keycap "1️⃣" (U+FE0F optional) -> its name "1" */
    if (m[0] >= '1' && m[0] <= '9' &&
        (!strcmp(m + 1, "\xef\xb8\x8f\xe2\x83\xa3") || !strcmp(m + 1, "\xe2\x83\xa3")))
        d[0] = m[0], m = d;
    for (i = 0; i < NMARKS; i++)
        if (!strcmp(m, mark_names[i].name)) return mark_names[i].icon;
    if (strlen(m) != 4 || (u[0] & 0xF8) != 0xF0 || (u[1] & 0xC0) != 0x80 ||
        (u[2] & 0xC0) != 0x80 || (u[3] & 0xC0) != 0x80) return NULL;
    cp = (u[0] & 7u) << 18 | (u[1] & 63u) << 12 | (u[2] & 63u) << 6 | (u[3] & 63u);
    return cp >= 0x1F300 && cp <= 0x1FAFF ? m : NULL;
}

/* `autocomplete emoji [X]`: no X -> "icon name" lines; a name -> its icon;
 * an icon (keycaps with or without U+FE0F) -> its name. 1 if unknown. */
int emoji_cmd(const char *x) {
    const char *ic = x ? snippet_mark(x) : NULL;
    size_t i;
    for (i = 0; i < NMARKS; i++) {
        const char *n = mark_names[i].name, *k = mark_names[i].icon;
        int len = (int)strcspn(k, "\xc2"); /* without the keycaps' U+00A0 */
        if (!x) printf("%s %s\n", k, n);
        else if (!strcmp(x, n)) return printf("%.*s\n", len, k), 0;
        else if (ic && !strcmp(ic, k)) return printf("%s\n", n), 0;
    }
    return x ? 1 : 0;
}

int check_groups(const ynode *n, const ynode *root, const char *path) {
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
    if (yaml_get(n, "snippets")) {
        char seen[128] = {0};
        for (g = yaml_get(n, "snippets")->child; g; g = g->next) {
            const ynode *k = yaml_get(g, "key");
            if (!snippet_key(k)) {
                fprintf(stderr, "%s: snippet \"%s\": key must be one of [0-9a-z]\n", path,
                        yaml_get(g, "command") ? yaml_get(g, "command")->str : "?");
                bad = 1;
            } else if (seen[(int)k->str[0]]++) {
                fprintf(stderr, "%s: duplicate snippet key %s\n", path, k->str);
                bad = 1;
            }
            if ((k = yaml_get(g, "mark")) && (k->type != Y_STR || !snippet_mark(k->str))) {
                fprintf(stderr, "%s: snippet \"%s\": mark must be an icon name (see docs/writing-specs.md) or one emoji (U+1F300..U+1FAFF, no U+FE0F)\n", path,
                        yaml_get(g, "command") ? yaml_get(g, "command")->str : "?");
                bad = 1;
            }
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

/* "alias_exec: CMD": user-defined aliases of a spec's subcommands (git's
 * alias.*), one "name expansion" line each (space or tab). Run once per
 * spec. */
#define MAXALIAS 256
static const ynode *g_al_spec;
static char *g_al_name[MAXALIAS], *g_al_exp[MAXALIAS];
static int g_nal;

static void load_aliases(const ynode *spec) {
    const ynode *ex = yaml_get(spec, "alias_exec");
    char shell[4096], line[2048];
    FILE *f;
    int i;
    if (spec == g_al_spec) return;
    g_al_spec = spec;
    g_nal = 0;
    if (!ex || ex->type != Y_STR) return;
    snprintf(shell, sizeof shell, "{ %s\n} 2>/dev/null", ex->str);
    if (!(f = popen(shell, "r"))) return;
    while (g_nal < MAXALIAS && fgets(line, sizeof line, f)) {
        char *sep;
        line[strcspn(line, "\r\n")] = 0;
        if (!(sep = strpbrk(line, " \t")) || sep == line) continue;
        *sep++ = 0;
        /* a later line (git: local config after global) overrides */
        for (i = 0; i < g_nal && strcmp(g_al_name[i], line); i++) {}
        if (i == g_nal) g_al_name[g_nal++] = strdup(line);
        g_al_exp[i] = strdup(sep + strspn(sep, " \t"));
    }
    pclose(f);
}

static const char *alias_exp(const ynode *spec, const char *name) {
    int i;
    load_aliases(spec);
    for (i = 0; i < g_nal; i++)
        if (!strcmp(g_al_name[i], name)) return g_al_exp[i];
    return NULL;
}

static const ynode *find_command(const ynode *cmds, const char *name);
static int find_flag(const ynode *cmd, const char *name, const ynode **val);
static int takes_value(const ynode *v);

/* Subcommand an alias stands for: the first word of its expansion that is
 * not a flag ("-c x=y" values skipped when the spec's flag takes one),
 * following alias chains. Shell aliases ("!...") only via a ": <cmd> <sub>"
 * hint, as in git's completion ("!f() { : git checkout ; ...}; f"). */
static const ynode *resolve_alias(const ynode *spec, const char *name, int depth) {
    const char *e = alias_exp(spec, name), *p;
    char word[256];
    if (!e || depth > 8) return NULL;
    if (*e == '!') {
        if (!(p = strstr(e, ": "))) return NULL;
        p += 2;
        p += strcspn(p, " \t;"); /* the command name */
    } else {
        p = e;
    }
    for (;;) {
        const ynode *s, *v = NULL;
        size_t len;
        p += strspn(p, " \t");
        if (!*p || *p == ';') return NULL;
        len = strcspn(p, " \t;");
        snprintf(word, sizeof word, "%.*s", (int)len, p);
        p += len;
        if (word[0] == '-') {
            if (!strchr(word, '=') && find_flag(spec, word, &v) && takes_value(v)) {
                p += strspn(p, " \t");
                p += strcspn(p, " \t;");
            }
            continue;
        }
        if ((s = find_command(yaml_get(spec, "commands"), word))) return s;
        return resolve_alias(spec, word, depth + 1);
    }
}

/* Emit every subcommand of `spec` whose "when" holds. "aliases" and
 * "alias_exec" renames are not listed, only shown in the description
 * ("desc (🔥 co, ch)"); an alias typed in full is emitted. */
static void emit_commands(const ynode *spec) {
    const ynode *cmds = yaml_get(spec, "commands"), *c, *a;
    int i;
    g_kind = "command";
    load_aliases(spec);
    /* aliases with arguments ("cob = checkout -b") are listed on their own */
    for (i = 0; i < g_nal; i++) {
        char buf[1024];
        if (find_command(cmds, g_al_exp[i])) continue;
        snprintf(buf, sizeof buf, "🔥 alias: %s", g_al_exp[i]);
        emit_desc(g_al_name[i], 1, strdup(buf));
    }
    for (c = cmds ? cmds->child : NULL; c; c = c->next) {
        const char *d = spec_desc(c);
        char buf[1024];
        size_t len, base;
        if (!when_ok(c)) continue;
        base = len = (size_t)snprintf(buf, sizeof buf, "%s%s(\xf0\x9f\x94\xa5 ", d ? d : "", d ? " " : "");
        /* static "aliases", then plain renames from "alias_exec" ("re = rebase") */
        for (a = yaml_get(c, "aliases") ? yaml_get(c, "aliases")->child : NULL; a; a = a->next)
            if (a->type == Y_STR && len < sizeof buf) {
                len += (size_t)snprintf(buf + len, sizeof buf - len, "%s%s", len == base ? "" : ", ", a->str);
                if (!strcmp(a->str, g_cur)) emit_desc(a->str, 1, d);
            }
        for (i = 0; i < g_nal; i++)
            if (!strcmp(g_al_exp[i], c->key) && len < sizeof buf) {
                len += (size_t)snprintf(buf + len, sizeof buf - len, "%s%s", len == base ? "" : ", ", g_al_name[i]);
                if (!strcmp(g_al_name[i], g_cur)) emit_desc(g_al_name[i], 1, d);
            }
        if (len == base) { emit_desc(c->key, 1, d); continue; }
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

/* `cache: SECONDS` on an exec value: its output is kept in
 * <cache dir>/exec/<hash> of (command, cwd, and the current word when the
 * command reads $AUTOCOMPLETE_CUR). Fills shell with a command that prints
 * the cached output, refreshing it when older than ttl. */
static int exec_cached(const char *ex, long ttl, char *shell, size_t size) {
    char dir[4096], cwd[4096] = "", path[4200];
    unsigned long h = 2166136261UL;
    const char *parts[3], *p;
    struct stat st;
    int i;
    if (ttl <= 0 || !cache_dir("exec", dir, sizeof dir) || strchr(dir, '\'')) return 0;
    if (!getcwd(cwd, sizeof cwd)) cwd[0] = 0;
    parts[0] = ex;
    parts[1] = cwd;
    parts[2] = strstr(ex, "AUTOCOMPLETE_CUR") ? g_cur : "";
    for (i = 0; i < 3; i++)
        for (p = parts[i];; p++) {
            h = ((h ^ (unsigned char)*p) * 16777619UL) & 0xffffffffUL;
            if (!*p) break;
        }
    snprintf(path, sizeof path, "%s/%08lx", dir, h);
    if (stat(path, &st) == 0 && time(NULL) - st.st_mtime < ttl)
        snprintf(shell, size, "cat '%s'", path);
    else
        snprintf(shell, size, "{ { %s\n} 2>/dev/null > '%s.'$$ && mv -f '%s.'$$ '%s'; } && cat '%s'", ex, path, path,
                 path, path);
    return 1;
}

static int is_true(const ynode *n) { return n && n->type == Y_BOOL && n->b; }

/* `snippets: true` on an exec value: its "key<TAB>value<TAB>desc[<TAB>mark]" lines
 * become snippets (add_snippets with an empty g_snippets_name: "@key" is
 * replaced by the whole value, single-quoted). */
static void exec_snippets(const char *shell) {
    static ynode *arr;
    char line[4096];
    FILE *f = popen(shell, "r");
    if (!f) return;
    if (!arr) arr = yaml_new(NULL, Y_ARR, NULL, NULL);
    while (fgets(line, sizeof line, f)) {
        char *val, *desc, *mark = NULL, *nl = strchr(line, '\n');
        ynode *e;
        if (nl) *nl = 0;
        if (!(val = strchr(line, '\t'))) continue;
        *val++ = 0;
        if ((desc = strchr(val, '\t'))) *desc++ = 0;
        e = yaml_new(arr, Y_OBJ, NULL, NULL);
        yaml_new(e, Y_STR, "key", line);
        yaml_new(e, Y_STR, "command", val);
        if (desc && (mark = strchr(desc, '\t'))) *mark++ = 0;
        if (desc && *desc) yaml_new(e, Y_STR, "description", desc);
        if (mark && *mark) yaml_new(e, Y_STR, "mark", mark);
    }
    pclose(f);
    g_snippets = arr;
    g_snippets_name = "";
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
        if (!strcmp(v->str, "file")) { g_kind = "file"; complete_paths(0, NULL); }
        else if (!strcmp(v->str, "dir")) { g_kind = "dir"; complete_paths(1, NULL); }
        else if (!strcmp(v->str, "@hosts")) complete_hosts();
        else if (!strcmp(v->str, "@users")) complete_users();
        else if (!strcmp(v->str, "@pids")) complete_pids();
        else if (!strcmp(v->str, "@processes")) complete_processes();
        else if (!strcmp(v->str, "@signals")) complete_signals();
        else if (!strcmp(v->str, "@make-targets")) { g_kind = "target"; complete_make_targets(); }
        else if (!strcmp(v->str, "@make-variables")) { g_kind = "var"; complete_make_variables(); }
        else if (!strcmp(v->str, "@subcommands")) emit_commands(parent);
        else if (!strcmp(v->str, "@command")) complete_commands();
        break;
    case Y_OBJ: {
        const ynode *ex = yaml_get(v, "exec"), *sx = yaml_get(v, "suffix"), *ns = yaml_get(v, "nospace"),
                    *tr = yaml_get(v, "tree"), *fl = yaml_get(v, "filter"), *gl = yaml_get(v, "file"),
                    *ca = yaml_get(v, "cache");
        char shell[8192];
        if (gl && gl->type == Y_STR) { g_kind = "file"; complete_paths(0, gl->str); break; }
        if (!ex || ex->type != Y_STR) break;
        if (!(ca && ca->type == Y_STR && exec_cached(ex->str, atol(ca->str), shell, sizeof shell)))
            snprintf(shell, sizeof shell, "{ %s\n} 2>/dev/null", ex->str);
        if (is_true(yaml_get(v, "snippets"))) { exec_snippets(shell); break; }
        g_kind = NULL;
        g_nofilter = fl && fl->type == Y_BOOL && !fl->b;
        setenv("AUTOCOMPLETE_CUR", g_cur, 1); /* the word being completed, unescaped */
        emit_cmd_output(shell, sx && sx->type == Y_STR ? sx->str : NULL,
                        !(ns && ns->type == Y_BOOL && ns->b),
                        tr && tr->type == Y_BOOL && tr->b ? '/' : tr && tr->type == Y_STR ? tr->str[0] : 0);
        g_nofilter = 0;
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

/* List cmd's flags, then (with_root) the root's (valid at every depth).
 * A switch already among the typed words isn't offered again, unless
 * `repeat: true`; a "-Dx=" key takes its value attached, so no space. */
static void list_flags(const ynode *c, const ynode *root, char **w, int n, int with_root) {
    const ynode *f, *s = c;
    int i;
    for (;;) {
        for (f = yaml_get(s, "flags") ? yaml_get(s, "flags")->child : NULL; f; f = f->next) {
            const ynode *gr;
            if (!yaml_get(f, "value") && !(yaml_get(f, "repeat") && yaml_get(f, "repeat")->b)) {
                for (i = 1; i < n - 1 && strcmp(w[i], f->key); i++) {}
                if (i < n - 1) continue;
            }
            gr = f->key[1] && !f->key[2] ? find_group(c, root, f->key[1]) : NULL;
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
        if (!with_root || s == root) break;
        s = root;
    }
}

/* The root's flags at command c: everywhere, unless the root says
 * `inherit_flags: false` (docker: global options only before the subcommand). */
static const ynode *root_flags(const ynode *root, const ynode *c) {
    const ynode *i = yaml_get(root, "inherit_flags");
    return c == root || !(i && i->type == Y_BOOL && !i->b) ? root : NULL;
}

int do_complete(const char *line, const char *wbreaks) {
    char *w[MAXW];
    size_t start[MAXW];
    int n = split(line, w, start), i, lvl = 0, npos = 0;
    const ynode *root, *c, *parent = NULL, *v = NULL;
    const char *cur, *slash, *cname;
    static char cpath[1024]; /* "docker volume prune": snippets are written in full */

    g_wbreaks = wbreaks;
    g_raw = line + start[n - 1];
    cur = g_cur = w[n - 1];

    slash = strrchr(w[0], '/');
    cname = slash ? slash + 1 : w[0];
    snprintf(cpath, sizeof cpath, "%s", cname);
    if (!(root = c = load_spec(cname))) {
        if (n > 1) { g_kind = "file"; complete_paths(0, NULL); } /* sudo cmd-without-spec */
        return 0;
    }
    if (is_str(yaml_get(root, "bridge"), "cobra")) {
        complete_cobra(w, n);
        return 0;
    }

    /* walk the already-typed words; lvl = index of the current command word */
    for (i = 1; i < n - 1; i++) {
        const ynode *s;
        if (!strcmp(w[i], "--")) continue;
        if (w[i][0] == '-') {
            if (strchr(w[i], '=')) continue;
            if ((find_flag(c, w[i], &v) || find_flag(root_flags(root, c), w[i], &v)) && takes_value(v)) {
                if (i + 1 < n - 1 && g_nseen < MAXSEEN) {
                    g_seen_flag[g_nseen] = w[i];
                    g_seen_val[g_nseen++] = w[i + 1];
                }
                i++; /* skip its value */
            }
            continue;
        }
        if ((s = find_command(yaml_get(c, "commands"), w[i])) || (s = resolve_alias(c, w[i], 0))) {
            parent = c; c = s; cname = w[i]; lvl = i; npos = 0;
            snprintf(cpath + strlen(cpath), sizeof cpath - strlen(cpath), " %s", s->key);
            continue;
        }
        if (is_str(yaml_get(c, "args"), "@command")) {
            if (strchr(w[i], '=') || !strcmp(w[i], "!")) continue; /* env NAME=value cmd, if ! cmd */
            /* the rest of the line is a command line of its own */
            g_nseen = 0;
            return do_complete(line + start[i], wbreaks);
        }
        npos++;
    }

    /* value for previous flag? */
    if (n >= 3 && w[n - 2][0] == '-' && !strchr(w[n - 2], '=')) {
        if ((find_flag(c, w[n - 2], &v) || find_flag(root_flags(root, c), w[n - 2], &v)) && takes_value(v)) {
            complete_value(v, c);
            return 0;
        }
    }

    /* --flag=value: '=' is a wordbreak, so complete only the part after it */
    /* -Dx=value: a key ending in '=' ("-Dx=") may have its own value */
    if (cur[0] == '-' && strchr(cur, '=')) {
        char name[256], namq[257];
        const char *eq = strchr(cur, '='), *req = strchr(g_raw, '=');
        snprintf(name, sizeof name, "%.*s", (int)(eq - cur), cur);
        snprintf(namq, sizeof namq, "%s=", name);
        if (find_flag(c, namq, &v) || find_flag(root_flags(root, c), namq, &v) ||
            (cur[1] == '-' && (find_flag(c, name, &v) || find_flag(root_flags(root, c), name, &v)))) {
            g_cur = eq + 1;
            if (req) g_raw = req + 1;
            /* -Dx='a b c: an unclosed quote holds several words; complete the last */
            if (req && (req[1] == '\'' || req[1] == '"') && !strchr(req + 2, req[1])) {
                const char *in = req + 2, *sp = strrchr(in, ' ');
                g_qpre = strndup(in, sp ? (size_t)(sp - in) + 1 : 0);
                g_cur = sp ? sp + 1 : in;
            }
            complete_value(v, c);
        }
        return 0;
    }

    if (cur[0] == '-') {
        list_flags(c, root, w, n, root_flags(root, c) != NULL);
        return 0;
    }

    emit_commands(c);
    /* snippets only right after the command word ("duti <TAB>"), not deeper in the line */
    g_snippets = n - 1 == lvl + 1 ? yaml_get(c, "snippets") : NULL;
    g_snippets_name = cpath;
    v = yaml_get(c, "args");
    /* "positional": the Nth positional word's value; past the list, "args" */
    {
        const ynode *p = yaml_get(c, "positional");
        for (p = p && p->type == Y_ARR ? p->child : NULL, i = 0; p && i < npos; p = p->next) i++;
        if (p) v = p;
    }
    /* "if <TAB>": the snippets, not every $PATH executable; "if g<TAB>" completes commands */
    if (g_snippets && !*g_cur && is_str(v, "@command")) return 0;
    if (g_snippets && *g_cur == '@') return 0; /* "wget @p": add_snippets */
    /* "@subcommands" on a leaf (docker help) lists the parent's commands */
    complete_value(v, is_str(v, "@subcommands") && parent ? parent : c);
    /* "docker compose down <TAB>": nothing else to offer, so the command's
     * own flags (root ones only after "-") */
    if (!*g_cur && !ncands() && !(g_snippets && g_snippets->child) && c != root) list_flags(c, root, w, n, 0);
    return 0;
}
