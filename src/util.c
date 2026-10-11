/* Small helpers: sorting, mkdir -p, terminal size and display width. */
#include <fcntl.h>
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
        if ((c & 0xC0) != 0x80) w += c >= 0xF0 ? 2 : 1;
    }
    return w;
}
