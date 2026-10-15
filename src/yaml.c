#include "yaml.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ynode *yaml_get(const ynode *map, const char *key) {
    ynode *c;
    if (!map || map->type != Y_OBJ) return NULL;
    for (c = map->child; c; c = c->next)
        if (!strcmp(c->key, key)) return c;
    return NULL;
}

void yaml_free(ynode *n) {
    while (n) {
        ynode *next = n->next;
        yaml_free(n->child);
        free(n->key);
        free(n->str);
        free(n);
        n = next;
    }
}

typedef struct {
    int ind;  /* leading spaces */
    char *s;  /* content: no indent, comment or trailing blanks */
    int no;   /* 1-based line number */
} line;

typedef struct {
    line *l;
    int n, i;
    const char *err;
    int errline;
} parser;

static ynode *node(ytype t) {
    ynode *n = calloc(1, sizeof *n);
    if (n) n->type = t;
    return n;
}

static ynode *fail(parser *ps, const char *msg, int no) {
    if (!ps->err) {
        ps->err = msg;
        ps->errline = no;
    }
    return NULL;
}

/* ------------------------------------------------------------ scalars */

/* Turn a plain scalar into null / bool / string. */
static ynode *plain(const char *s, size_t len) {
    ynode *n;
    while (len && (s[len - 1] == ' ' || s[len - 1] == '\t')) len--;
    if (!len || (len == 1 && *s == '~') || (len == 4 && !strncmp(s, "null", 4)))
        return node(Y_NULL);
    if (len == 4 && !strncmp(s, "true", 4)) { n = node(Y_BOOL); n->b = 1; return n; }
    if (len == 5 && !strncmp(s, "false", 5)) return node(Y_BOOL);
    n = node(Y_STR);
    n->str = malloc(len + 1);
    memcpy(n->str, s, len);
    n->str[len] = 0;
    return n;
}

/* Quoted scalar at *p ('..' or ".."); advances *p past the closing quote. */
static char *quoted(const char **p) {
    char q = **p, *out = malloc(strlen(*p) + 1), *o = out;
    const char *s = *p + 1;
    for (; *s; s++) {
        if (*s == q) {
            if (q == '\'' && s[1] == '\'') { *o++ = '\''; s++; continue; }
            *o = 0;
            *p = s + 1;
            return out;
        }
        if (q == '"' && *s == '\\' && s[1]) {
            switch (*++s) {
            case 'n': *o++ = '\n'; break;
            case 't': *o++ = '\t'; break;
            case 'r': *o++ = '\r'; break;
            case '0': *o++ = '\0'; break;
            default: *o++ = *s; break; /* \" \\ \/ and anything else */
            }
            continue;
        }
        *o++ = *s;
    }
    free(out);
    return NULL; /* unterminated */
}

/* ---------------------------------------------------------------- flow */

static void ws(const char **p) {
    while (**p == ' ' || **p == '\t') (*p)++;
}

/* Parse a flow value: {..}, [..], quoted, or plain. In flow context plain
 * scalars stop at , ] } (and at ':' for keys). */
static ynode *flow(parser *ps, const char **p, int inflow, int key, int no) {
    ynode *n;
    ws(p);
    if (**p == '{' || **p == '[') {
        char close = **p == '{' ? '}' : ']';
        ynode **tail;
        n = node(close == '}' ? Y_OBJ : Y_ARR);
        tail = &n->child;
        (*p)++;
        ws(p);
        if (**p == close) { (*p)++; return n; }
        for (;;) {
            ynode *k = NULL, *v;
            if (close == '}') {
                if (!(k = flow(ps, p, 1, 1, no)) || k->type == Y_ARR || k->type == Y_OBJ) {
                    yaml_free(k);
                    break;
                }
                ws(p);
                if (**p != ':') { yaml_free(k); fail(ps, "expected ':' in flow mapping", no); break; }
                (*p)++;
            }
            if (!(v = flow(ps, p, 1, 0, no))) { yaml_free(k); break; }
            if (k) {
                v->key = k->str ? k->str : strdup(k->type == Y_NULL ? "null" : k->b ? "true" : "false");
                k->str = NULL;
                yaml_free(k);
            }
            *tail = v;
            tail = &v->next;
            ws(p);
            if (**p == ',') { (*p)++; ws(p); if (**p == close) { (*p)++; return n; } continue; }
            if (**p == close) { (*p)++; return n; }
            fail(ps, close == '}' ? "expected ',' or '}'" : "expected ',' or ']'", no);
            break;
        }
        yaml_free(n);
        return NULL;
    }
    if (**p == '"' || **p == '\'') {
        char *s = quoted(p);
        if (!s) return fail(ps, "unterminated string", no);
        n = node(Y_STR);
        n->str = s;
        return n;
    }
    {
        const char *start = *p;
        while (**p && !(inflow && strchr(",]}", **p)) && !(key && **p == ':')) (*p)++;
        return plain(start, (size_t)(*p - start));
    }
}

/* Inline value after "key:" or "- ": a flow value that must use the line. */
static ynode *inline_value(parser *ps, const char *s, int no) {
    const char *p = s;
    ynode *n;
    if (*s != '{' && *s != '[' && *s != '"' && *s != '\'') return plain(s, strlen(s));
    if (!(n = flow(ps, &p, 0, 0, no))) return NULL;
    ws(&p);
    if (*p) { yaml_free(n); return fail(ps, "unexpected text after value", no); }
    return n;
}

/* --------------------------------------------------------------- block */

static int is_seq(const char *s) { return s[0] == '-' && (s[1] == 0 || s[1] == ' '); }

/* Offset of the "key: value" colon outside quotes/flow, or -1. */
static int key_colon(const char *s) {
    char q = 0;
    int i;
    if (*s == '{' || *s == '[') return -1;
    for (i = 0; s[i]; i++) {
        if (q) {
            if (q == '"' && s[i] == '\\' && s[i + 1]) i++;             /* "\"" */
            else if (q == '\'' && s[i] == q && s[i + 1] == '\'') i++;  /* 'it''s' */
            else if (s[i] == q) q = 0;
            continue;
        }
        if ((s[i] == '"' || s[i] == '\'') && i == 0) { q = s[i]; continue; }
        if (s[i] == ':' && (s[i + 1] == 0 || s[i + 1] == ' ')) return i;
    }
    return -1;
}

static ynode *block(parser *ps, int ind);

/* Value of a "key:" / "-" with nothing after it: an indented block, a
 * sequence at the same indent (mappings only), or null. */
static ynode *nested(parser *ps, int ind, int allow_same_seq) {
    if (ps->i < ps->n && (ps->l[ps->i].ind > ind ||
                          (allow_same_seq && ps->l[ps->i].ind == ind && is_seq(ps->l[ps->i].s))))
        return block(ps, ps->l[ps->i].ind);
    return node(Y_NULL);
}

static ynode *mapping(parser *ps, int ind) {
    ynode *n = node(Y_OBJ), **tail = &n->child;
    while (ps->i < ps->n && ps->l[ps->i].ind == ind && !is_seq(ps->l[ps->i].s)) {
        line *l = &ps->l[ps->i];
        int c = key_colon(l->s);
        char *key, *val;
        ynode *v;
        if (c < 0) { fail(ps, "expected 'key: value'", l->no); break; }
        if (l->s[0] == '"' || l->s[0] == '\'') {
            const char *p = l->s;
            if (!(key = quoted(&p))) { fail(ps, "unterminated string", l->no); break; }
        } else {
            ynode *k = plain(l->s, (size_t)c);
            key = k->str ? k->str : strdup("null");
            k->str = NULL;
            yaml_free(k);
        }
        for (val = l->s + c + 1; *val == ' '; val++) {}
        ps->i++;
        v = *val ? inline_value(ps, val, l->no) : nested(ps, ind, 1);
        if (!v) { free(key); break; }
        v->key = key;
        *tail = v;
        tail = &v->next;
    }
    if (!ps->err && ps->i < ps->n && ps->l[ps->i].ind > ind)
        fail(ps, "bad indentation", ps->l[ps->i].no);
    if (ps->err) { yaml_free(n); return NULL; }
    return n;
}

static ynode *sequence(parser *ps, int ind) {
    ynode *n = node(Y_ARR), **tail = &n->child;
    while (ps->i < ps->n && ps->l[ps->i].ind == ind && is_seq(ps->l[ps->i].s)) {
        line *l = &ps->l[ps->i];
        char *rest = l->s + 1;
        ynode *v;
        while (*rest == ' ') rest++;
        if (!*rest) {
            ps->i++;
            v = nested(ps, ind, 0);
        } else if (is_seq(rest) || key_colon(rest) >= 0) {
            /* "- key: v" / "- - x": the rest starts a block at its column */
            l->ind += (int)(rest - l->s);
            l->s = rest;
            v = block(ps, l->ind);
        } else {
            ps->i++;
            v = inline_value(ps, rest, l->no);
        }
        if (!v) break;
        *tail = v;
        tail = &v->next;
    }
    if (!ps->err && ps->i < ps->n && ps->l[ps->i].ind > ind)
        fail(ps, "bad indentation", ps->l[ps->i].no);
    if (ps->err) { yaml_free(n); return NULL; }
    return n;
}

static ynode *block(parser *ps, int ind) {
    return is_seq(ps->l[ps->i].s) ? sequence(ps, ind) : mapping(ps, ind);
}

/* ----------------------------------------------------------------- api */

/* Strip a " #" comment outside quotes; returns 0 if the line has a tab indent. */
static void strip_comment(char *s) {
    char q = 0;
    int i;
    for (i = 0; s[i]; i++) {
        if (q) {
            if (q == '"' && s[i] == '\\' && s[i + 1]) i++;             /* "\"" */
            else if (q == '\'' && s[i] == q && s[i + 1] == '\'') i++;  /* 'it''s' */
            else if (s[i] == q) q = 0;
            continue;
        }
        if (s[i] == '"' || s[i] == '\'') {
            /* quotes only open at the start of a value ("key: '..'", "- '..'",
             * flow items); inside a plain scalar like don't they are literal */
            int j = i;
            while (j > 0 && s[j - 1] == ' ') j--;
            if (j == 0 || (strchr(":-", s[j - 1]) && j < i) || strchr("[{,", s[j - 1])) q = s[i];
            continue;
        }
        if (s[i] == '#' && (i == 0 || s[i - 1] == ' ' || s[i - 1] == '\t')) { s[i] = 0; break; }
    }
}

ynode *yaml_parse(const char *text, char *err, int errlen) {
    parser ps;
    char *buf = strdup(text), *p = buf;
    ynode *root = NULL;
    int cap = 64, no = 0;

    memset(&ps, 0, sizeof ps);
    ps.l = malloc(sizeof(line) * (size_t)cap);
    while (p && *p) {
        char *nl = strchr(p, '\n'), *s = p;
        size_t len;
        int ind = 0;
        no++;
        if (nl) *nl = 0;
        p = nl ? nl + 1 : NULL;
        while (*s == ' ') { s++; ind++; }
        if (*s == '\t') { fail(&ps, "tabs are not allowed for indentation", no); break; }
        strip_comment(s);
        len = strlen(s);
        while (len && (s[len - 1] == ' ' || s[len - 1] == '\t' || s[len - 1] == '\r')) s[--len] = 0;
        if (!len || !strcmp(s, "---")) continue;
        if (ps.n == cap) ps.l = realloc(ps.l, sizeof(line) * (size_t)(cap *= 2));
        ps.l[ps.n].ind = ind;
        ps.l[ps.n].s = s;
        ps.l[ps.n++].no = no;
    }

    if (!ps.err) {
        if (!ps.n) root = node(Y_OBJ);
        else if ((root = block(&ps, ps.l[0].ind)) && ps.i < ps.n) {
            yaml_free(root);
            root = fail(&ps, "unexpected content", ps.l[ps.i].no);
        }
    }
    if (!root && err) snprintf(err, (size_t)errlen, "line %d: %s", ps.errline, ps.err ? ps.err : "out of memory");
    free(ps.l);
    free(buf);
    return root;
}

ynode *yaml_load(const char *path, char *err, int errlen) {
    FILE *f = fopen(path, "rb");
    char *buf;
    long len;
    ynode *n;
    if (!f) {
        if (err) snprintf(err, (size_t)errlen, "cannot open");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)len + 1);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        if (err) snprintf(err, (size_t)errlen, "read error");
        return NULL;
    }
    buf[len] = 0;
    fclose(f);
    n = yaml_parse(buf, err, errlen);
    free(buf);
    return n;
}

/* ------------------------------------------------------------- writing */

ynode *yaml_new(ynode *parent, ytype t, const char *key, const char *str) {
    ynode *n = node(t), **tail;
    if (!n) return NULL;
    if (key) n->key = strdup(key);
    if (str) n->str = strdup(str);
    if (parent) {
        for (tail = &parent->child; *tail; tail = &(*tail)->next) {}
        *tail = n;
    }
    return n;
}

/* Plain when it can't be mistaken for anything else, else 'single-quoted'. */
static void put_scalar(FILE *f, const char *s) {
    const char *p;
    int plain_ok = *s && *s != '-' && *s != '@' && strcmp(s, "null") && strcmp(s, "true") &&
                   strcmp(s, "false") && strcmp(s, "~");
    for (p = s; *p && plain_ok; p++)
        if (!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_./", *p)) plain_ok = 0;
    if (plain_ok || (*s == '-' && (isalpha((unsigned char)s[1]) || s[1] == '-') && !strpbrk(s, " :#'\"{}[],&*!|>%@`"))) {
        fputs(s, f);
        return;
    }
    fputc('\'', f);
    for (p = s; *p; p++) {
        if (*p == '\'') fputc('\'', f);
        fputc(*p, f);
    }
    fputc('\'', f);
}

static void write_inline(FILE *f, const ynode *n) {
    switch (n->type) {
    case Y_NULL: break;
    case Y_BOOL: fputs(n->b ? "true" : "false", f); break;
    case Y_STR: put_scalar(f, n->str); break;
    case Y_ARR: fputs("[]", f); break;
    case Y_OBJ: fputs("{}", f); break;
    }
}

static void write_block(FILE *f, const ynode *n, int ind) {
    const ynode *c;
    for (c = n->child; c; c = c->next) {
        fprintf(f, "%*s", ind, "");
        if (n->type == Y_OBJ) {
            put_scalar(f, c->key);
            fputc(':', f);
        } else
            fputc('-', f);
        if ((c->type == Y_OBJ || c->type == Y_ARR) && c->child) {
            fputc('\n', f);
            write_block(f, c, ind + 2);
        } else {
            if (c->type != Y_NULL) fputc(' ', f);
            write_inline(f, c);
            fputc('\n', f);
        }
    }
}

void yaml_write(FILE *f, const ynode *n) {
    if ((n->type == Y_OBJ || n->type == Y_ARR) && n->child) write_block(f, n, 0);
    else {
        write_inline(f, n);
        fputc('\n', f);
    }
}
