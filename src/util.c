/* Small helpers: sorting, mkdir -p, terminal size and display width. */
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include "util.h"

int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* mkdir -p */
void mkdirs(char *dir) {
    char *s;
    for (s = dir + 1; *s; s++)
        if (*s == '/') { *s = 0; mkdir(dir, 0755); *s = '/'; }
    mkdir(dir, 0755);
}

int term_width(void) {
    struct winsize ws;
    int fd = open("/dev/tty", O_RDONLY), w = 0;
    if (fd >= 0) {
        if (ioctl(fd, TIOCGWINSZ, &ws) == 0) w = ws.ws_col;
        close(fd);
    }
    if (w <= 0 && getenv("COLUMNS")) w = atoi(getenv("COLUMNS"));
    return w > 20 ? w : 80;
}

int term_rows(void) {
    struct winsize ws;
    int fd = open("/dev/tty", O_RDONLY), h = 0;
    if (fd >= 0) {
        if (ioctl(fd, TIOCGWINSZ, &ws) == 0) h = ws.ws_row;
        close(fd);
    }
    if (h <= 0 && getenv("LINES")) h = atoi(getenv("LINES"));
    return h > 5 ? h : 24;
}

/* Terminal columns of a UTF-8 string: continuation bytes take none and
 * 4-byte sequences (emoji) take two. */
int dwidth(const char *s) {
    int w = 0;
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == 0xEF && !memcmp(s, "\xef\xb8\x8f", 3)) { s += 2; continue; } /* U+FE0F */
        if (c == 0xE2 && !memcmp(s, "\xe2\x83\xa3", 3)) { s += 2; continue; } /* U+20E3 keycap: 0 */
        if ((c & 0xC0) != 0x80) w += c >= 0xF0 ? 2 : 1;
    }
    return w;
}

int find_exe(const char *name, char *out, size_t size) {
    char *path = getenv("PATH"), *copy, *tok, p[PATH_MAX];
    struct stat st;
    int found = 0;
    if (!path) return 0;
    copy = strdup(path);
    for (tok = strtok(copy, ":"); tok && !found; tok = strtok(NULL, ":")) {
        snprintf(p, sizeof p, "%s/%s", *tok ? tok : ".", name);
        found = stat(p, &st) == 0 && S_ISREG(st.st_mode) && access(p, X_OK) == 0;
    }
    free(copy);
    if (found && out) snprintf(out, size, "%s", p);
    return found;
}
