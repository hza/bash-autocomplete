/* Built-in completers: files, make targets/variables, $PATH commands,
 * exec output; flag values seen on the line. */
#include <ctype.h>
#include <dirent.h>
#include <fnmatch.h>
#include <pwd.h>
#include <signal.h>
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

/* Does name match one of the space-separated fnmatch patterns? */
static int glob_ok(const char *name, const char *globs) {
    char pat[256];
    const char *p = globs, *e;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        for (e = p; *e && *e != ' '; e++) {}
        snprintf(pat, sizeof pat, "%.*s", (int)(e - p), p);
        if (!fnmatch(pat, name, 0)) return 1;
        p = e;
    }
    return 0;
}

void complete_files(int dirs_only, const char *globs) {
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
        if (globs && !isdir && !glob_ok(e->d_name, globs)) continue;
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
    if (strchr(g_cur, '/')) { g_kind = "file"; complete_files(0, NULL); return; }
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

/* "~user/" for a word like "~ro" (no '/' yet); system "_" accounts skipped. */
static int complete_tilde_users(void) {
    struct passwd *pw;
    char cand[300];
    if (g_cur[0] != '~' || strchr(g_cur, '/')) return 0;
    g_kind = "dir";
    setpwent();
    while ((pw = getpwent())) {
        if (pw->pw_name[0] == '_') continue;
        snprintf(cand, sizeof cand, "~%s/", pw->pw_name);
        emit(cand, 0);
    }
    endpwent();
    return 1;
}

void complete_paths(int dirs_only, const char *globs) {
    if (complete_tilde_users()) return;
    complete_files(dirs_only, globs);
}

/* Login users, without the "_" system accounts. */
void complete_users(void) {
    struct passwd *pw;
    g_kind = NULL;
    setpwent();
    while ((pw = getpwent()))
        if (pw->pw_name[0] != '_') emit_desc(pw->pw_name, 1, pw->pw_gecos && *pw->pw_gecos ? strdup(pw->pw_gecos) : NULL);
    endpwent();
}

/* Signal names without "SIG" (HUP, KILL, ...). */
void complete_signals(void) {
    int i;
    char name[32];
    g_kind = NULL;
    for (i = 1; i < NSIG; i++) {
        size_t k;
        snprintf(name, sizeof name, "%s", sys_signame[i]);
        for (k = 0; name[k]; k++) name[k] = (char)toupper((unsigned char)name[k]);
        emit(name, 1);
    }
}

/* Hosts from ~/.ssh/config ("Host" names without patterns) and
 * ~/.ssh/known_hosts (plain names; hashed entries and bare IPs skipped). */
void complete_hosts(void) {
    char path[4096], line[4096], *tok, *save;
    const char *home = getenv("HOME");
    FILE *f;
    if (!home) return;
    g_kind = NULL;
    snprintf(path, sizeof path, "%s/.ssh/config", home);
    if ((f = fopen(path, "r"))) {
        while (fgets(line, sizeof line, f)) {
            char *p = line;
            while (*p == ' ' || *p == '\t') p++;
            if (strncasecmp(p, "host", 4) || (p[4] != ' ' && p[4] != '\t')) continue;
            for (tok = strtok_r(p + 4, " \t\r\n", &save); tok; tok = strtok_r(NULL, " \t\r\n", &save))
                if (!strpbrk(tok, "*?!")) emit(tok, 1);
        }
        fclose(f);
    }
    snprintf(path, sizeof path, "%s/.ssh/known_hosts", home);
    if ((f = fopen(path, "r"))) {
        while (fgets(line, sizeof line, f)) {
            char *sp = strpbrk(line, " \t");
            if (line[0] == '|' || line[0] == '#' || line[0] == '@' || !sp) continue;
            *sp = 0;
            for (tok = strtok_r(line, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
                char *e;
                if (*tok == '[') { /* [host]:port */
                    tok++;
                    if ((e = strchr(tok, ']'))) *e = 0;
                }
                /* skip IPv4 and IPv6 addresses */
                if (tok[strspn(tok, "0123456789.")] && !strchr(tok, ':')) emit(tok, 1);
            }
        }
        fclose(f);
    }
}

/* Process IDs of the user's processes, described by the program name. */
void complete_pids(void) {
    char line[1024];
    FILE *f = popen("ps -xo pid=,comm= 2>/dev/null", "r");
    if (!f) return;
    g_kind = NULL;
    while (fgets(line, sizeof line, f)) {
        char *p = line, *pid, *comm, *slash;
        line[strcspn(line, "\n")] = 0;
        while (*p == ' ') p++;
        pid = p;
        while (*p && *p != ' ') p++;
        if (!*p) continue;
        *p++ = 0;
        while (*p == ' ') p++;
        comm = (slash = strrchr(p, '/')) ? slash + 1 : p;
        emit_desc(pid, 1, strdup(comm));
    }
    pclose(f);
}

/* Names of the user's running programs. */
void complete_processes(void) {
    char line[1024];
    FILE *f = popen("ps -xco comm= 2>/dev/null", "r");
    if (!f) return;
    g_kind = NULL;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\n")] = 0;
        if (*line && *line != '-' && *line != '<') emit(line, 1);
    }
    pclose(f);
}

/* Append s to buf single-quoted for sh. */
static void shquote(char *buf, size_t size, const char *s) {
    size_t n = strlen(buf);
    if (n + 2 >= size) return;
    buf[n++] = ' ';
    buf[n++] = '\'';
    for (; *s && n + 5 < size; s++) {
        if (*s == '\'') { memcpy(buf + n, "'\\''", 4); n += 4; }
        else buf[n++] = *s;
    }
    buf[n++] = '\'';
    buf[n] = 0;
}

/* Cobra programs (`bridge: cobra`): `CMD __complete WORDS... CUR` prints
 * "name<TAB>desc" lines and a last line ":<directive>" (bits: 1 error,
 * 2 no space, 4 no file fallback, 8 lines are file extensions, 16 dirs). */
void complete_cobra(char **words, int n) {
    char shell[16384] = "", line[2048], *lines[4096];
    int i, nl = 0, dir = 0;
    FILE *f;
    shquote(shell, sizeof shell, words[0]);
    strncat(shell, " __complete", sizeof shell - strlen(shell) - 1);
    for (i = 1; i < n; i++) shquote(shell, sizeof shell, words[i]);
    strncat(shell, " </dev/null 2>/dev/null", sizeof shell - strlen(shell) - 1);
    if (!(f = popen(shell, "r"))) return;
    while (fgets(line, sizeof line, f) && nl < 4096) {
        line[strcspn(line, "\r\n")] = 0;
        lines[nl++] = strdup(line);
    }
    pclose(f);
    if (nl && lines[nl - 1][0] == ':') dir = atoi(lines[--nl] + 1);
    else dir = 1;
    if (dir & 1) return;
    g_kind = NULL;
    /* "--flag=val": cobra answers with bare values; complete after the '=' */
    if (g_cur[0] == '-' && strchr(g_cur, '=') && strchr(g_raw, '=')) {
        g_cur = strchr(g_cur, '=') + 1;
        g_raw = strchr(g_raw, '=') + 1;
    }
    if (dir & 8) { /* extensions: files with those, plus dirs */
        char globs[2048] = "";
        for (i = 0; i < nl; i++) {
            size_t k = strlen(globs);
            snprintf(globs + k, sizeof globs - k, "*.%s ", lines[i]);
        }
        g_kind = "file";
        complete_paths(0, globs);
        return;
    }
    if (dir & 16) { g_kind = "dir"; complete_paths(1, NULL); return; }
    for (i = 0; i < nl; i++) {
        char *desc = strchr(lines[i], '\t');
        if (desc) *desc++ = 0;
        if (*lines[i]) emit_desc(lines[i], !(dir & 2), desc && *desc && strcmp(desc, lines[i]) ? desc : NULL);
    }
    if (!nl && !(dir & 4)) { g_kind = "file"; complete_paths(0, NULL); }
}
