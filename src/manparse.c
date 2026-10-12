/* Man page -> spec. Works on the plain text of `man -P cat CMD | col -bx`.
 *
 * Option entries are lines indented like the other option entries and made
 * of one or more comma-separated options, then either the end of the line
 * or two spaces and the description:
 *
 *      -A num, --after-context=num
 *              Print num lines of trailing context after each match.
 *      -c      Create a new archive containing the specified items.
 *
 * An option takes a value when its entry or the SYNOPSIS (`[-O fmt]`) shows
 * an argument. Continuation lines are the ones indented deeper. */
#include "manparse.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define MAXLINES 20000
#define MAXOPTS 8
#define MAXSYN 128

typedef struct {
    char name[64];
    char arg[64]; /* "" when none */
} opt;

static int indent(const char *s) {
    int i = 0;
    while (s[i] == ' ') i++;
    return i;
}

/* Parse one option "-x arg" / "--long=arg" / "--long <a b>" at *p. */
static int parse_opt(const char **pp, opt *o) {
    const char *p = *pp;
    size_t n = 0;
    if (p[0] != '-' || p[1] == ' ' || !p[1]) return 0;
    o->name[n++] = *p++;
    if (*p == '-') o->name[n++] = *p++;
    if (!*p || *p == ' ' || *p == ',') return 0;
    o->name[n++] = *p++; /* -@, -1, -% */
    while ((isalnum((unsigned char)*p) || *p == '-' || *p == '_') && n < sizeof o->name - 1)
        o->name[n++] = *p++;
    o->name[n] = 0;
    if (*p && !strchr(" ,=[", *p)) return 0;
    if (n < 2 || (o->name[1] == '-' && !isalnum((unsigned char)o->name[2]))) return 0;
    o->arg[0] = 0;
    /* "=arg", "[=arg]", " arg", " <arg words>", " [arg]" */
    if (*p == '=' || *p == '[' || (*p == ' ' && p[1] && p[1] != ' ' && p[1] != '-')) {
        const char *s = *p == ' ' ? p + 1 : p;
        int depth = 0;
        n = 0;
        if (*s == '=') s++;
        while (*s && (depth || (*s != ' ' && *s != ','))) {
            if (*s == '<' || *s == '[' || *s == '{') depth++;
            else if ((*s == '>' || *s == ']' || *s == '}') && depth) depth--;
            if (n < sizeof o->arg - 1) o->arg[n++] = *s;
            s++;
        }
        o->arg[n] = 0;
        if (depth) return 0;
        p = s;
    }
    *pp = p;
    return 1;
}

/* An option entry line (indent already skipped)? Fills opts, *desc = text
 * after the options on the same line (or ""). Returns option count. */
static int parse_entry(const char *s, opt *opts, const char **desc) {
    int n = 0;
    for (;;) {
        if (n == MAXOPTS || !parse_opt(&s, &opts[n])) return 0;
        n++;
        if (s[0] == ',' && s[1] == ' ' && s[2] == '-') { s += 2; continue; }
        if (!*s) { *desc = s; return n; }
        if (s[0] == ' ' && s[1] == ' ') {
            while (*s == ' ') s++;
            *desc = s;
            return n;
        }
        return 0;
    }
}

/* SYNOPSIS arguments: "[-O fmt | -o fmt] [-G gid[,gid...]]" -> O:fmt ... */
/* help: the "usage:" line and its indented continuation lines instead, and
 * options without an argument too ("[--append]"; not "-abc" bundles). */
static int synopsis_args(char **lines, int nl, opt *syn, int help) {
    int i, n = 0, in = 0;
    for (i = 0; i < nl && n < MAXSYN; i++) {
        const char *p;
        if (!in && (help ? !strncasecmp(lines[i], "usage:", 6) : !strncmp(lines[i], "SYNOPSIS", 8))) {
            in = 1;
            if (!help) continue;
        } else if (!in) continue;
        else if (lines[i][0] && lines[i][0] != ' ') break;
        for (p = lines[i]; (p = strchr(p, '-')) && n < MAXSYN; p++) {
            const char *q = p;
            if (p == lines[i] || !(p[-1] == '[' || (p[-1] == ' ' && p - 1 > lines[i] && p[-2] == '|'))) continue;
            if (!parse_opt(&q, &syn[n])) continue;
            if (help ? syn[n].name[1] == '-' || !syn[n].name[2] : syn[n].arg[0] && syn[n].arg[0] != '-') n++;
        }
    }
    return n;
}

static const char *value_kind(const char *arg) {
    char low[64];
    size_t i;
    for (i = 0; arg[i] && i < sizeof low - 1; i++) low[i] = (char)tolower((unsigned char)arg[i]);
    low[i] = 0;
    if (strstr(low, "dir")) return "dir";
    if (strstr(low, "file") || strstr(low, "path")) return "file";
    return "any";
}

/* First sentence of text, at most ~256 chars, cut at a word boundary. */
static void sentence(const char *text, char *out, size_t size) {
    size_t n = 0, cut = 0;
    const char *p;
    for (p = text; *p && n < size - 1; p++) {
        if (*p == '.' && (p[1] == ' ' || !p[1]) && !(p > text + 1 && p[-2] == 'e' && p[-1] == '.')) break;
        if (n >= 256) { if (cut) n = cut; out[n] = 0; strcat(out, "..."); return; }
        if (*p == ' ') cut = n;
        out[n++] = *p;
    }
    out[n] = 0;
}

static ynode *add_flag(ynode *flags, const char *name, const char *arg, const char *desc) {
    ynode *f;
    if (yaml_get(flags, name)) return NULL;
    f = yaml_new(flags, Y_OBJ, name, NULL);
    if (arg && *arg) yaml_new(f, Y_STR, "value", value_kind(arg));
    if (desc && *desc) yaml_new(f, Y_STR, "description", desc);
    return f;
}

/* "Commands:" / "Available Commands:" sections of --help output: lines
 * "  name   description" (one word, then 2+ spaces or the end of the line)
 * until a blank or unindented line. */
static void help_commands(char **lines, int nl, ynode *root) {
    ynode *cmds = NULL, *c;
    char name[64], low[128], desc[300];
    int i, in = 0;
    for (i = 0; i < nl; i++) {
        const char *l = lines[i], *p, *d;
        size_t len = strlen(l), k;
        if (len && l[0] != ' ' && l[len - 1] == ':') {
            for (k = 0; k < len && k < sizeof low - 1; k++) low[k] = (char)tolower((unsigned char)l[k]);
            low[k] = 0;
            in = strstr(low, "command") != NULL;
            continue;
        }
        if (!in) continue;
        if (!*l || l[0] != ' ') { in = 0; continue; }
        for (p = l; *p == ' '; p++) {}
        if (!isalnum((unsigned char)*p)) continue;
        for (d = p; isalnum((unsigned char)*d) || strchr("-_:.", *d); d++) {}
        if (*d && !(d[0] == ' ' && (d[1] == ' ' || !d[1]))) continue;
        snprintf(name, sizeof name, "%.*s", (int)(d - p), p);
        while (*d == ' ') d++;
        if (!cmds) cmds = yaml_new(root, Y_OBJ, "commands", NULL);
        if (yaml_get(cmds, name)) continue;
        c = yaml_new(cmds, Y_OBJ, name, NULL);
        sentence(d, desc, sizeof desc);
        if (*desc) yaml_new(c, Y_STR, "description", desc);
    }
}

/* Per-command usage blocks of --help output (jfr):
 *
 *  jfr print [--xml|--json]
 *            [--events <filter>]
 *            <file>
 *      Print contents of a recording file.
 *
 * A line "<cmd> <name> ..." starts a command; deeper-indented lines starting
 * with "[" or "<" continue its synopsis, the first other one is its
 * description. "[-x arg]" / "[-a|-b]" become flags, "<file>" makes args
 * file, "[<command>]" @subcommands. */
static void help_usages(char **lines, int nl, const char *cmd, ynode *root) {
    ynode *cmds = NULL, *c = NULL, *flags = NULL;
    char name[64], desc[300], buf[1024];
    size_t cl = strlen(cmd);
    int i, ind = 0, gotdesc = 0;
    for (i = 0; i < nl; i++) {
        const char *l = lines[i], *p = l + indent(l), *d;
        char *q;
        int k = indent(l);
        if (!strncmp(p, cmd, cl) && p[cl] == ' ' && isalpha((unsigned char)p[cl + 1])) {
            for (d = p + cl + 1; isalnum((unsigned char)*d) || strchr("-_", *d); d++) {}
            if (*d && *d != ' ') { c = NULL; continue; }
            snprintf(name, sizeof name, "%.*s", (int)(d - p - cl - 1), p + cl + 1);
            if (!cmds) cmds = yaml_new(root, Y_OBJ, "commands", NULL);
            if (yaml_get(cmds, name)) { c = NULL; continue; }
            c = yaml_new(cmds, Y_OBJ, name, NULL);
            flags = NULL;
            ind = k;
            gotdesc = 0;
        } else if (!c || k <= ind) {
            c = NULL;
            continue;
        } else if (*p != '[' && *p != '<') {
            if (!gotdesc) {
                sentence(p, desc, sizeof desc);
                if (*desc) yaml_new(c, Y_STR, "description", desc);
                gotdesc = 1;
            }
            continue;
        } else d = p;
        if (gotdesc) continue;
        snprintf(buf, sizeof buf, "%s", d);
        if ((q = strstr(buf, " ("))) *q = 0; /* "(aliases --help, -h)" */
        for (q = buf; *q; q++)
            if (*q == '|' || *q == ']') *q = ' ';
        if (strstr(buf, "[<command>")) yaml_new(c, Y_STR, "args", "@subcommands");
        else if (strstr(buf, "file>") && !yaml_get(c, "args")) yaml_new(c, Y_STR, "args", "file");
        for (q = buf; (q = strchr(q, '-')); q++) {
            const char *s = q;
            opt o;
            if (q > buf && q[-1] != '[' && q[-1] != ' ') continue;
            if (!parse_opt(&s, &o)) continue;
            if (!flags) flags = yaml_new(c, Y_OBJ, "flags", NULL);
            add_flag(flags, o.name, o.arg, NULL);
            q = (char *)s - 1;
        }
    }
}

/* Intro line of --help output: the first unindented line before any option,
 * "Header:" or other indented text, skipping the usage block, version
 * banners ("rg 14.1.0") and "Name <mail>" lines. "" if none. */
static void help_intro(char **lines, int nl, char *out, size_t size) {
    int i;
    *out = 0;
    for (i = 0; i < nl; i++) {
        const char *l = lines[i], *sp = strchr(l, ' ');
        size_t len = strlen(l);
        if (!len) continue;
        if (!strncasecmp(l, "usage", 5)) {
            while (i + 1 < nl && lines[i + 1][0] == ' ' && lines[i + 1][indent(lines[i + 1])] != '-') i++;
            continue;
        }
        if (l[0] == ' ' || l[0] == '-' || l[len - 1] == ':') return;
        if (!sp || isdigit((unsigned char)sp[1]) || (sp[1] == 'v' && isdigit((unsigned char)sp[2]))) continue;
        if (strchr(l, '<') && strchr(l, '@')) continue;
        { /* the whole paragraph: wrapped lines up to a blank one */
            char text[1024];
            snprintf(text, sizeof text, "%s", l);
            while (++i < nl && lines[i][0] && lines[i][0] != ' ' && strlen(text) < 400) {
                strncat(text, " ", sizeof text - strlen(text) - 1);
                strncat(text, lines[i], sizeof text - strlen(text) - 1);
            }
            sentence(text, out, size);
        }
        return;
    }
}

/* root's description: desc, else the whatis line, else "" (always present,
 * first key). */
static void set_desc(ynode *root, const char *cmd, const char *desc) {
    char shell[512], line[1024], buf[300] = "";
    const char *p;
    ynode *d;
    FILE *f;
    if (yaml_get(root, "description")) return;
    if (desc && *desc) snprintf(buf, sizeof buf, "%s", desc);
    else {
        snprintf(shell, sizeof shell, "man -f '%s' 2>/dev/null", cmd);
        if ((f = popen(shell, "r"))) {
            while (fgets(line, sizeof line, f)) {
                line[strcspn(line, "\r\n")] = 0;
                /* "jfr(1)  - ..." / "ls(1), gls(1) - ..."; first exact name */
                if (strncmp(line, cmd, strlen(cmd)) || line[strlen(cmd)] != '(' || !(p = strstr(line, " - "))) continue;
                sentence(p + 3, buf, sizeof buf);
                break;
            }
            pclose(f);
        }
    }
    d = yaml_new(root, Y_STR, "description", buf);
    if (root->child != d) { /* move to front */
        ynode *q = root->child;
        while (q->next != d) q = q->next;
        q->next = NULL;
        d->next = root->child;
        root->child = d;
    }
}

/* Spec from the text `shell` prints: a man page (help = 0) or --help
 * output (help = 1: no NAME/SYNOPSIS, "Commands:" sections). */
static ynode *text_spec(const char *shell, const char *cmd, int help) {
    char buf[4096], *lines[MAXLINES], text[2048], desc[300];
    int nl = 0, i, j, ind = -1, counts[64] = {0}, nsyn;
    const char *p;
    opt opts[MAXOPTS], syn[MAXSYN];
    ynode *root, *flags;
    FILE *f;

    if (!(f = popen(shell, "r"))) return NULL;
    while (nl < MAXLINES && fgets(buf, sizeof buf, f)) {
        size_t len = strcspn(buf, "\r\n");
        while (len && buf[len - 1] == ' ') len--;
        buf[len] = 0;
        lines[nl++] = strdup(buf);
    }
    pclose(f);

    /* the indent shared by most option entries */
    for (i = 0; i < nl; i++) {
        const char *d;
        int k = indent(lines[i]);
        if ((k > 0 || help) && k < 64 && parse_entry(lines[i] + k, opts, &d)) counts[k]++;
    }
    for (i = help ? 0 : 1; i < 64; i++)
        if (counts[i] && (ind < 0 || counts[i] > counts[ind])) ind = i;
    if (ind < 0 && !help) {
        for (i = 0; i < nl; i++) free(lines[i]);
        return NULL;
    }

    root = yaml_new(NULL, Y_OBJ, NULL, NULL);
    for (i = 0; i < nl && !help; i++)
        if (!strcmp(lines[i], "NAME") && i + 1 < nl && (p = strstr(lines[i + 1], " - "))) {
            sentence(p + 3, desc, sizeof desc);
            yaml_new(root, Y_STR, "description", desc);
            break;
        }
    flags = yaml_new(root, Y_OBJ, "flags", NULL);
    nsyn = synopsis_args(lines, nl, syn, help);

    for (i = 0; i < nl && ind >= 0; i++) {
        const char *d;
        int n, o;
        if (indent(lines[i]) != ind || !(n = parse_entry(lines[i] + ind, opts, &d))) continue;
        /* description: rest of this line + deeper-indented lines after it */
        snprintf(text, sizeof text, "%s", d);
        for (j = i + 1; j < nl && indent(lines[j]) > ind && strlen(text) < 400; j++) {
            if (*text) strncat(text, " ", sizeof text - strlen(text) - 1);
            strncat(text, lines[j] + indent(lines[j]), sizeof text - strlen(text) - 1);
        }
        sentence(text[0] == ':' ? text + 1 + strspn(text + 1, " ") : text, desc, sizeof desc); /* python: "-b     : issue..." */
        for (o = 0; o < n; o++) {
            const char *arg = opts[o].arg;
            int s;
            for (j = 0; j < n && !*arg; j++) arg = opts[j].arg; /* "-A num, --after-context" */
            for (s = 0; s < nsyn && !*arg; s++)
                if (!strcmp(syn[s].name, opts[o].name)) arg = syn[s].arg;
            add_flag(flags, opts[o].name, arg, desc);
        }
    }
    /* SYNOPSIS-only options ("[-O fmt]" with no entry of its own) */
    for (i = 0; i < nsyn; i++) add_flag(flags, syn[i].name, syn[i].arg, NULL);

    if (help) help_commands(lines, nl, root);
    if (help && !yaml_get(root, "commands")) help_usages(lines, nl, cmd, root);
    if (!flags->child) { /* no empty "flags: {}" */
        ynode **pp = &root->child;
        while (*pp != flags) pp = &(*pp)->next;
        *pp = flags->next;
        flags->next = NULL;
        yaml_free(flags);
        flags = NULL;
    }
    if (!flags && !yaml_get(root, "commands")) {
        for (i = 0; i < nl; i++) free(lines[i]);
        yaml_free(root);
        return NULL;
    }
    if (help) help_intro(lines, nl, desc, sizeof desc);
    else *desc = 0;
    set_desc(root, cmd, desc);
    for (i = 0; i < nl; i++) free(lines[i]);
    if (!yaml_get(root, "commands")) yaml_new(root, Y_STR, "args", "file");
    return root;
}

static int plain_name(const char *cmd) {
    const char *p;
    for (p = cmd; *p; p++)
        if (!isalnum((unsigned char)*p) && !strchr("._+-", *p)) return 0;
    return *cmd != 0;
}

ynode *man_spec(const char *cmd) {
    char shell[512];
    if (!plain_name(cmd)) return NULL;
    snprintf(shell, sizeof shell, "MANWIDTH=1000 man -P cat '%s' 2>/dev/null | col -bx", cmd);
    return text_spec(shell, cmd, 0);
}

ynode *help_spec(const char *cmd) {
    char shell[512];
    if (!plain_name(cmd)) return NULL;
    /* stdin from /dev/null: a command that ignores --help must not wait for input */
    snprintf(shell, sizeof shell, "'%s' --help </dev/null 2>&1 | col -bx", cmd);
    return text_spec(shell, cmd, 1);
}

ynode *cobra_spec(const char *cmd) {
    char shell[512], line[1024], last[1024] = "";
    ynode *root;
    FILE *f;
    if (!plain_name(cmd)) return NULL;
    snprintf(shell, sizeof shell, "'%s' __complete '' </dev/null 2>/dev/null", cmd);
    if (!(f = popen(shell, "r"))) return NULL;
    while (fgets(line, sizeof line, f)) snprintf(last, sizeof last, "%s", line);
    pclose(f);
    if (last[0] != ':' || !isdigit((unsigned char)last[1])) return NULL;
    root = yaml_new(NULL, Y_OBJ, NULL, NULL);
    yaml_new(root, Y_STR, "bridge", "cobra");
    {   /* cobra always handles --help itself */
        char *lines[64], desc[300] = "";
        int nl = 0, i;
        snprintf(shell, sizeof shell, "'%s' --help </dev/null 2>/dev/null", cmd);
        if ((f = popen(shell, "r"))) {
            while (nl < 64 && fgets(line, sizeof line, f)) {
                size_t len = strcspn(line, "\r\n");
                while (len && line[len - 1] == ' ') len--;
                line[len] = 0;
                lines[nl++] = strdup(line);
            }
            pclose(f);
        }
        help_intro(lines, nl, desc, sizeof desc);
        for (i = 0; i < nl; i++) free(lines[i]);
        set_desc(root, cmd, desc);
    }
    return root;
}
