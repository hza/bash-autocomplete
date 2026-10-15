/* Spec lookup (search dirs, recursive) and loading. */
#ifndef AUTOCOMPLETE_SPEC_H
#define AUTOCOMPLETE_SPEC_H

#include <stddef.h>
#include "yaml.h"

/* Max subfolder depth searched below each spec dir (guards symlink loops). */
#define SPEC_DEPTH 4

/* Lookup dirs: $AUTOCOMPLETE_SPECS, ~/.config/autocomplete,
 * <bin dir>/specs, <bin dir>/../share/autocomplete. Call once first. */
void init_dirs(void);
/* Path of the YAML spec for `name` (first dir that has it), or 0. */
int find_spec(const char *name, char *path, size_t size);
/* Like find_spec, but skipping ~/.config/autocomplete (the shipped spec). */
int find_bundled(const char *name, char *path, size_t size);
/* $XDG_CACHE_HOME/autocomplete/<sub> or ~/.cache/autocomplete/<sub> ("" =
 * the top dir), created; 0 when unset or AUTOCOMPLETE_NOCACHE is set. */
int cache_dir(const char *sub, char *dir, size_t size);
/* Load a spec; exports AUTOCOMPLETE_SPEC_DIR. */
ynode *load_spec(const char *name);
/* Spec from `name`'s man page, cached in <cache dir>/man (`bash --man`). */
ynode *load_man_spec(const char *name);
/* Name of the last loaded spec after resolving symlinks (fly -> "flyctl"). */
extern char g_spec_real[256];
/* "description" of a command or flag spec, if any. */
const char *spec_desc(const ynode *spec);
/* Call fn for every unique spec name. */
void each_spec(void (*fn)(const char *));

#endif
