/* Spec lookup (search dirs, recursive), loading and the compiled cache. */
#ifndef AUTOCOMPLETE_SPEC_H
#define AUTOCOMPLETE_SPEC_H

#include <stddef.h>
#include "yaml.h"

/* Max subfolder depth searched below each spec dir (guards symlink loops). */
#define SPEC_DEPTH 4

/* Only YAML bigger than this is compiled to a cache; smaller specs parse
 * about as fast as the cache loads. */
#define CACHE_MIN (100 * 1024)

/* Lookup dirs: $AUTOCOMPLETE_SPECS, ~/.config/autocomplete,
 * <bin dir>/specs, <bin dir>/../share/autocomplete. Call once first. */
void init_dirs(void);
/* Path of the YAML spec for `name` (first dir that has it), or 0. */
int find_spec(const char *name, char *path, size_t size);
/* $XDG_CACHE_HOME/autocomplete/<sub> or ~/.cache/autocomplete/<sub> ("" =
 * the top dir), created; 0 when unset or AUTOCOMPLETE_NOCACHE is set. */
int cache_dir(const char *sub, char *dir, size_t size);
/* Compile n into the cache for `name`; out receives the cache path. */
int save_cache(const char *name, const ynode *n, const char *src, char *out, size_t size);
/* Load a spec (from its cache when big and fresh); exports AUTOCOMPLETE_SPEC_DIR. */
ynode *load_spec(const char *name);
/* "description" of a command or flag spec, if any. */
const char *spec_desc(const ynode *spec);
/* Call fn for every unique spec name. */
void each_spec(void (*fn)(const char *));

#endif
