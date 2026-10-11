/* Built-in completers: files, make targets/variables, $PATH commands,
 * exec output; flag values seen on the line. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "builtins.h"
#include "output.h"

/* flag values seen on the line, e.g. make -C dir */
const char *g_seen_flag[MAXSEEN], *g_seen_val[MAXSEEN];
int g_nseen;

static const char *seen(const char *a, const char *b, const char *c) {
    int i;
    for (i = g_nseen - 1; i >= 0; i--) {
        const char *f = g_seen_flag[i];
        if (!strcmp(f, a) || (b && !strcmp(f, b)) || (c && !strcmp(f, c))) return g_seen_val[i];
    }
    return NULL;
}

/* tree: separator char to fold lines at (0 = no folding) */
void emit_cmd_output(const char *shell, const char *suffix, int space, char tree) {
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
        /* tree: fold "a/b" into "a/" past the typed prefix, like paths. Display-only
         * names (with U+00A0, e.g. duti's "ts2: uti") are never folded. */
        if (tree && !strncmp(line, g_cur, clen) && !strstr(line, "\xc2\xa0") &&
            (slash = strchr(line + clen, tree))) {
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

void complete_files(int dirs_only) {
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

void complete_make_targets(void) {
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
void complete_make_variables(void) {
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

/* Executables on $PATH (a word with '/' completes as a path). */
void complete_commands(void) {
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
