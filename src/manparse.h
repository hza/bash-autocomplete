/* Build a completion spec from a command's man page. */
#ifndef AUTOCOMPLETE_MANPARSE_H
#define AUTOCOMPLETE_MANPARSE_H

#include "yaml.h"

/* {description, flags: {name: {value?, description?}}, args: file} from
 * `man CMD`, or NULL when there is no man page or it lists no options. */
ynode *man_spec(const char *cmd);
/* The same from `CMD --help` (no description; "Commands:" sections become
 * commands without flags of their own), or NULL when it lists neither. */
ynode *help_spec(const char *cmd);
/* {description, bridge: cobra} when `CMD __complete ""` answers like a
 * cobra program (last line ":<directive>"), else NULL. */
ynode *cobra_spec(const char *cmd);

#endif
