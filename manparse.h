/* Build a completion spec from a command's man page. */
#ifndef AUTOCOMPLETE_MANPARSE_H
#define AUTOCOMPLETE_MANPARSE_H

#include "yaml.h"

/* {description, flags: {name: {value?, description?}}, args: file} from
 * `man CMD`, or NULL when there is no man page or it lists no options. */
ynode *man_spec(const char *cmd);

#endif
