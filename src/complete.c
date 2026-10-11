/* Spec walk: split the line, follow subcommands and flags, complete the
 * value under the cursor. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
        snprintf(buf, sizeof buf, "alias: %s", g_al_exp[i]);
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
        else if (!strcmp(v->str, "@subcommands")) emit_commands(parent);
        else if (!strcmp(v->str, "@command")) complete_commands();
        break;
    case Y_OBJ: {
        const ynode *ex = yaml_get(v, "exec"), *sx = yaml_get(v, "suffix"), *ns = yaml_get(v, "nospace"),
                    *tr = yaml_get(v, "tree"), *fl = yaml_get(v, "filter");
        char shell[4096];
        if (!ex || ex->type != Y_STR) break;
        snprintf(shell, sizeof shell, "{ %s\n} 2>/dev/null", ex->str);
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

int do_complete(const char *line, const char *wbreaks) {
    char *w[MAXW];
    size_t start[MAXW];
    int n = split(line, w, start), i, lvl = 0, npos = 0;
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

    /* walk the already-typed words; lvl = index of the current command word */
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
        if ((s = find_command(yaml_get(c, "commands"), w[i])) || (s = resolve_alias(c, w[i], 0))) { parent = c; c = s; cname = w[i]; lvl = i; npos = 0; continue; }
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
        if ((find_flag(c, w[n - 2], &v) || find_flag(root, w[n - 2], &v)) && takes_value(v)) {
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
        if (find_flag(c, namq, &v) || find_flag(root, namq, &v) ||
            (cur[1] == '-' && (find_flag(c, name, &v) || find_flag(root, name, &v)))) {
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

    emit_commands(c);
    /* examples only right after the command word ("duti <TAB>"), not deeper in the line */
    g_examples = n - 1 == lvl + 1 ? yaml_get(c, "examples") : NULL;
    g_examples_title = spec_desc(c);
    g_examples_name = cname;
    v = yaml_get(c, "args");
    /* "positional": the Nth positional word's value; past the list, "args" */
    {
        const ynode *p = yaml_get(c, "positional");
        for (p = p && p->type == Y_ARR ? p->child : NULL, i = 0; p && i < npos; p = p->next) i++;
        if (p) v = p;
    }
    /* "if <TAB>": the examples, not every $PATH executable; "if g<TAB>" completes commands */
    if (g_examples && !*g_cur && is_str(v, "@command")) return 0;
    /* "@subcommands" on a leaf (docker help) lists the parent's commands */
    complete_value(v, is_str(v, "@subcommands") && parent ? parent : c);
    return 0;
}
