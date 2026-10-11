/* Small helpers: sorting, mkdir -p, terminal size and display width. */
#ifndef AUTOCOMPLETE_UTIL_H
#define AUTOCOMPLETE_UTIL_H

/* qsort comparator for char * arrays. */
int cmp_str(const void *a, const void *b);
/* mkdir -p (dir is modified temporarily). */
void mkdirs(char *dir);
/* Terminal columns / rows: tty, else $COLUMNS / $LINES, else 80 / 24. */
int term_width(void);
int term_rows(void);
/* Terminal columns of a UTF-8 string (4-byte sequences = 2). */
int dwidth(const char *s);

#endif
