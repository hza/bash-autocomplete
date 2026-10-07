/* Minimal JSON parser: enough for completion specs. */
#ifndef AUTOCOMPLETE_JSON_H
#define AUTOCOMPLETE_JSON_H

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jtype;

typedef struct jnode {
    jtype type;
    char *key;           /* member name when inside an object */
    char *str;           /* J_STR */
    double num;          /* J_NUM, J_BOOL (0/1) */
    struct jnode *child; /* first element/member (J_ARR, J_OBJ) */
    struct jnode *next;  /* next sibling */
} jnode;

/* Parse text; returns NULL and fills err (if given) on failure. */
jnode *json_parse(const char *text, char *err, int errlen);
/* Read and parse a whole file; NULL if missing or invalid. */
jnode *json_load(const char *path, char *err, int errlen);
/* Member of an object by key, or NULL. */
jnode *json_get(const jnode *obj, const char *key);
void json_free(jnode *n);

#endif
