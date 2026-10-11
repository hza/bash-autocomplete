/* Spec walk: split the line, follow subcommands and flags, complete the
 * value under the cursor. */
#ifndef AUTOCOMPLETE_COMPLETE_H
#define AUTOCOMPLETE_COMPLETE_H

#include "yaml.h"

/* Queue candidates for the last word of line (call flush() after). */
int do_complete(const char *line, const char *wbreaks);
/* `check`: report "groups" letters without a "-x" flag; 1 if any. */
int example_key(const ynode *k);
int check_groups(const ynode *n, const ynode *root, const char *path);

#endif
