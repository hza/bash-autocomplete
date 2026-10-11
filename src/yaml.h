/* Minimal YAML parser for completion specs.
 *
 * Supported: block mappings and sequences (indented with spaces), "- key: v"
 * items, flow {..} and [..], plain / 'single' / "double" quoted scalars,
 * null (empty, ~, null), true/false, # comments, "---".
 * Not supported: anchors/aliases, tags, multi-line and block (| >) scalars.
 * All other scalars (including numbers) stay strings. */
#ifndef AUTOCOMPLETE_YAML_H
#define AUTOCOMPLETE_YAML_H

#include <stdio.h>

typedef enum { Y_NULL, Y_BOOL, Y_STR, Y_ARR, Y_OBJ } ytype;

typedef struct ynode {
    ytype type;
    char *key;           /* member name when inside a mapping */
    char *str;           /* Y_STR */
    int b;               /* Y_BOOL */
    struct ynode *child; /* first element/member (Y_ARR, Y_OBJ) */
    struct ynode *next;  /* next sibling */
} ynode;

/* Parse text; returns NULL and fills err (if given) on failure. */
ynode *yaml_parse(const char *text, char *err, int errlen);
/* Read and parse a whole file; NULL if missing or invalid. */
ynode *yaml_load(const char *path, char *err, int errlen);
/* Member of a mapping by key, or NULL. */
ynode *yaml_get(const ynode *map, const char *key);
void yaml_free(ynode *n);

/* Build trees: a new node (key/str copied, may be NULL), appended to
 * parent's children when parent is given. */
ynode *yaml_new(ynode *parent, ytype t, const char *key, const char *str);
/* Write a tree as block YAML that yaml_parse reads back unchanged. */
void yaml_write(FILE *f, const ynode *n);

/* Compiled form: write a tree to `path` (atomically) tagged with its source
 * file `src`; load it back, NULL if missing, corrupt or made from another src. */
int yaml_save_bin(const ynode *n, const char *path, const char *src);
ynode *yaml_load_bin(const char *path, const char *src);

#endif
