/* Candidate queue: emit() escapes, filters and trims; flush() folds
 * (vars, flags, groups, examples) and prints the final list. */
#ifndef AUTOCOMPLETE_OUTPUT_H
#define AUTOCOMPLETE_OUTPUT_H

#include "yaml.h"

enum { SHOW_DEFAULT, SHOW_ALWAYS, SHOW_COLLAPSED };

extern const char *g_cur;   /* current word, unescaped (for filtering) */
extern const char *g_raw;   /* current word as typed (for wordbreak trimming) */
extern const char *g_wbreaks;
extern char *g_qpre;        /* inside an open quote: text after it up to the last space */

/* Attributes given to candidates emitted now */
extern const char *g_kind;  /* source, e.g. "target"; tagged when kinds mix */
extern int g_show;          /* SHOW_* */
extern int g_noesc;         /* display-only: no shell escaping */
extern int g_nofilter;      /* exec `filter: false`: skip the g_cur prefix check */
extern const char *g_group, *g_gdesc; /* "groups" key and its description */

/* Spec "examples" listed on an empty word, with the command word and its description */
extern const ynode *g_examples;
extern const char *g_examples_name;

/* Queue a candidate; space: finished word (append a space). */
void emit_desc(const char *cand, int space, const char *desc);
void emit(const char *cand, int space);
/* Fold and print all queued candidates. Call once. */
/* Number of queued candidates. */
int ncands(void);
void flush(void);

#endif
