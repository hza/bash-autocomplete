/* Spec lookup (search dirs, recursive) and loading. */
#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#include "spec.h"
#include "manparse.h"
#include "util.h"

#define MAXDIRS 16

static char *g_dirs[MAXDIRS];
static int g_ndirs;

static void add_dir(const char *d) {
    if (g_ndirs < MAXDIRS && d && *d) g_dirs[g_ndirs++] = strdup(d);
}

void init_dirs(void) {
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
    {
        ssize_t len = readlink("/proc/self/exe", exe, sizeof exe - 1);
        if (len <= 0) return;
        exe[len] = 0;
    }
#endif
    if (!(slash = strrchr(exe, '/'))) return;
    *slash = 0;
    snprintf(buf, sizeof buf, "%s/specs", exe);
    add_dir(buf);
    snprintf(buf, sizeof buf, "%s/../share/autocomplete", exe);
    add_dir(realpath(buf, exe) ? exe : buf); /* no `..` in printed paths */
}

static const char *g_exts[] = {".yaml", ".yml", 0};

static int is_subdir(const char *dir, const struct dirent *e) {
    char p[PATH_MAX];
    struct stat st;
    if (e->d_name[0] == '.') return 0;
    snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Spec for `name` in `dir`, else in its subfolders (recursively, sorted). */
static int find_in(const char *dir, const char *name, char *path, size_t size, int depth) {
    char *subs[256], sub[PATH_MAX];
    struct dirent *e;
    DIR *d;
    int e2, n = 0, i, found = 0;
    for (e2 = 0; g_exts[e2]; e2++) {
        snprintf(path, size, "%s/%s%s", dir, name, g_exts[e2]);
        if (access(path, R_OK) == 0) return 1;
    }
    if (depth >= SPEC_DEPTH || !(d = opendir(dir))) return 0;
    while ((e = readdir(d)) && n < 256)
        if (is_subdir(dir, e)) subs[n++] = strdup(e->d_name);
    closedir(d);
    qsort(subs, n, sizeof *subs, cmp_str);
    for (i = 0; i < n; i++) {
        snprintf(sub, sizeof sub, "%s/%s", dir, subs[i]);
        if (!found) found = find_in(sub, name, path, size, depth + 1);
        free(subs[i]);
    }
    return found;
}

/* Path of the YAML spec for `name` (first dir that has it, searched
 * recursively), or 0. */
int find_spec(const char *name, char *path, size_t size) {
    int i;
    if (strchr(name, '/') || name[0] == '.') return 0;
    for (i = 0; i < g_ndirs; i++)
        if (find_in(g_dirs[i], name, path, size, 0)) return 1;
    return 0;
}

int find_bundled(const char *name, char *path, size_t size) {
    char user[PATH_MAX];
    int i;
    if (strchr(name, '/') || name[0] == '.') return 0;
    snprintf(user, sizeof user, "%s/.config/autocomplete", getenv("HOME") ? getenv("HOME") : "");
    for (i = 0; i < g_ndirs; i++)
        if (strcmp(g_dirs[i], user) && find_in(g_dirs[i], name, path, size, 0)) return 1;
    return 0;
}

/* $XDG_CACHE_HOME/autocomplete/<sub> or ~/.cache/autocomplete/<sub>, created. */
int cache_dir(const char *sub, char *dir, size_t size) {
    const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    if (getenv("AUTOCOMPLETE_NOCACHE")) return 0;
    if (xdg && *xdg) snprintf(dir, size, "%s/autocomplete%s%s", xdg, *sub ? "/" : "", sub);
    else if (home) snprintf(dir, size, "%s/.cache/autocomplete%s%s", home, *sub ? "/" : "", sub);
    else return 0;
    mkdirs(dir);
    return 1;
}

#ifdef __APPLE__
#define MTIME(st) (st).st_mtimespec
#else
#define MTIME(st) (st).st_mtim
#endif

char g_spec_real[256];

/* Load a spec from its YAML. */
ynode *load_spec(const char *name) {
    char path[PATH_MAX], bin[PATH_MAX], err[256];
    ynode *n;
    if (!find_spec(name, path, sizeof path)) return NULL;
    /* exec commands find helper files next to the (symlink-resolved) spec */
    if (realpath(path, bin)) {
        char *b = strrchr(bin, '/'), *dot;
        snprintf(g_spec_real, sizeof g_spec_real, "%s", b + 1);
        if ((dot = strrchr(g_spec_real, '.'))) *dot = 0;
        *b = 0;
        setenv("AUTOCOMPLETE_SPEC_DIR", bin, 1);
    }
    n = yaml_load(path, err, sizeof err);
    if (!n && getenv("AUTOCOMPLETE_DEBUG")) fprintf(stderr, "autocomplete: %s: %s\n", path, err);
    return n;
}

/* Spec generated from `name`'s man page, kept in <cache dir>/man/NAME.yaml
 * (empty when the page lists no options); regenerated when the executable is
 * newer. NULL: no man spec. */
ynode *load_man_spec(const char *name) {
    char dir[PATH_MAX], yml[PATH_MAX], exe[PATH_MAX];
    struct stat es, cs;
    ynode *n;
    FILE *f;
    if (!find_exe(name, exe, sizeof exe) || stat(exe, &es) || !cache_dir("man", dir, sizeof dir)) return NULL;
    snprintf(yml, sizeof yml, "%s/%s.yaml", dir, name);
    if (stat(yml, &cs) == 0 && MTIME(cs).tv_sec >= MTIME(es).tv_sec)
        return cs.st_size ? yaml_load(yml, NULL, 0) : NULL;
    n = man_spec(name);
    if ((f = fopen(yml, "w"))) {
        if (n) yaml_write(f, n);
        fclose(f);
    }
    return n;
}

/* "description" of a command or flag spec, if any. */
const char *spec_desc(const ynode *spec) {
    const ynode *d = yaml_get(spec, "description");
    return d && d->type == Y_STR ? d->str : NULL;
}

/* Add spec names found in dir and its subfolders to names (deduplicated). */
static void collect_specs(const char *dir, char **names, int *nn, int max, int depth) {
    char sub[PATH_MAX];
    DIR *d = opendir(dir);
    struct dirent *e;
    int j;
    if (!d) return;
    while ((e = readdir(d)) && *nn < max) {
        size_t len = strlen(e->d_name), xl = 0;
        int x;
        if (e->d_name[0] == '.') continue;
        if (is_subdir(dir, e)) {
            snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name);
            if (depth < SPEC_DEPTH) collect_specs(sub, names, nn, max, depth + 1);
            continue;
        }
        for (x = 0; g_exts[x]; x++) {
            xl = strlen(g_exts[x]);
            if (len > xl && !strcmp(e->d_name + len - xl, g_exts[x])) break;
        }
        if (!g_exts[x]) continue;
        e->d_name[len - xl] = 0;
        for (j = 0; j < *nn && strcmp(names[j], e->d_name); j++) {}
        if (j == *nn) names[(*nn)++] = strdup(e->d_name);
    }
    closedir(d);
}

/* Call fn for every unique spec name (lookup order picks the file). */
void each_spec(void (*fn)(const char *)) {
    char *names[2048];
    int nn = 0, i;
    for (i = 0; i < g_ndirs; i++) collect_specs(g_dirs[i], names, &nn, 2048, 0);
    for (i = 0; i < nn; i++) fn(names[i]);
}
