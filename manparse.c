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
static int synopsis_args(char **lines, int nl, opt *syn) {
    int i, n = 0, in = 0;
    for (i = 0; i < nl && n < MAXSYN; i++) {
        const char *p;
        if (!strncmp(lines[i], "SYNOPSIS", 8)) { in = 1; continue; }
        if (!in) continue;
        if (lines[i][0] && lines[i][0] != ' ') break;
        for (p = lines[i]; (p = strchr(p, '-')) && n < MAXSYN; p++) {
            const char *q = p;
            if (p == lines[i] || !(p[-1] == '[' || (p[-1] == ' ' && p - 1 > lines[i] && p[-2] == '|'))) continue;
            if (parse_opt(&q, &syn[n]) && syn[n].arg[0] && syn[n].arg[0] != '-') n++;
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

/* First sentence of text, at most ~90 chars, cut at a word boundary. */
static void sentence(const char *text, char *out, size_t size) {
    size_t n = 0, cut = 0;
    const char *p;
    for (p = text; *p && n < size - 1; p++) {
        if (*p == '.' && (p[1] == ' ' || !p[1]) && !(p > text + 1 && p[-2] == 'e' && p[-1] == '.')) break;
        if (n >= 90) { if (cut) n = cut; out[n] = 0; strcat(out, "..."); return; }
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

ynode *man_spec(const char *cmd) {
    char shell[512], buf[4096], *lines[MAXLINES], text[2048], desc[128];
    int nl = 0, i, j, ind = -1, counts[64] = {0}, nsyn;
    const char *p;
    opt opts[MAXOPTS], syn[MAXSYN];
    ynode *root, *flags;
    FILE *f;

    for (p = cmd; *p; p++)
        if (!isalnum((unsigned char)*p) && !strchr("._+-", *p)) return NULL;
    snprintf(shell, sizeof shell, "MANWIDTH=1000 man -P cat '%s' 2>/dev/null | col -bx", cmd);
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
        if (k > 0 && k < 64 && parse_entry(lines[i] + k, opts, &d)) counts[k]++;
    }
    for (i = 1; i < 64; i++)
        if (counts[i] && (ind < 0 || counts[i] > counts[ind])) ind = i;
    if (ind < 0) {
        for (i = 0; i < nl; i++) free(lines[i]);
        return NULL;
    }

    root = yaml_new(NULL, Y_OBJ, NULL, NULL);
    for (i = 0; i < nl; i++)
        if (!strcmp(lines[i], "NAME") && i + 1 < nl && (p = strstr(lines[i + 1], " - "))) {
            sentence(p + 3, desc, sizeof desc);
            yaml_new(root, Y_STR, "description", desc);
            break;
        }
    flags = yaml_new(root, Y_OBJ, "flags", NULL);
    nsyn = synopsis_args(lines, nl, syn);

    for (i = 0; i < nl; i++) {
        const char *d;
        int n, o;
        if (indent(lines[i]) != ind || !(n = parse_entry(lines[i] + ind, opts, &d))) continue;
        /* description: rest of this line + deeper-indented lines after it */
        snprintf(text, sizeof text, "%s", d);
        for (j = i + 1; j < nl && indent(lines[j]) > ind && strlen(text) < 400; j++) {
            if (*text) strncat(text, " ", sizeof text - strlen(text) - 1);
            strncat(text, lines[j] + indent(lines[j]), sizeof text - strlen(text) - 1);
        }
        sentence(text, desc, sizeof desc);
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

    for (i = 0; i < nl; i++) free(lines[i]);
    if (!flags->child) {
        yaml_free(root);
        return NULL;
    }
    yaml_new(root, Y_STR, "args", "file");
    return root;
}
