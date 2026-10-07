#include "json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *p;
    const char *err;
} parser;

static void ws(parser *ps) {
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r') ps->p++;
}

static jnode *node(jtype t) {
    jnode *n = calloc(1, sizeof *n);
    if (n) n->type = t;
    return n;
}

/* Append UTF-8 encoding of code point cp. */
static size_t utf8(char *out, unsigned cp) {
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | cp >> 6);
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    out[0] = (char)(0xE0 | cp >> 12);
    out[1] = (char)(0x80 | (cp >> 6 & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

static char *string(parser *ps) {
    const char *start = ++ps->p; /* skip opening quote */
    char *out, *o;
    /* decoded string is never longer than the source */
    while (*ps->p && *ps->p != '"') ps->p += *ps->p == '\\' && ps->p[1] ? 2 : 1;
    if (*ps->p != '"') { ps->err = "unterminated string"; return NULL; }
    o = out = malloc((size_t)(ps->p - start) + 1);
    if (!out) return NULL;
    for (ps->p = start; *ps->p != '"'; ps->p++) {
        if (*ps->p != '\\') { *o++ = *ps->p; continue; }
        switch (*++ps->p) {
        case 'n': *o++ = '\n'; break;
        case 't': *o++ = '\t'; break;
        case 'r': *o++ = '\r'; break;
        case 'b': *o++ = '\b'; break;
        case 'f': *o++ = '\f'; break;
        case 'u': {
            unsigned cp = 0;
            int i;
            for (i = 1; i <= 4; i++) {
                char c = ps->p[i];
                cp <<= 4;
                if (c >= '0' && c <= '9') cp |= (unsigned)(c - '0');
                else if (c >= 'a' && c <= 'f') cp |= (unsigned)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') cp |= (unsigned)(c - 'A' + 10);
                else { ps->err = "bad \\u escape"; free(out); return NULL; }
            }
            ps->p += 4;
            o += utf8(o, cp);
            break;
        }
        default: *o++ = *ps->p; break; /* \" \\ \/ */
        }
    }
    ps->p++; /* closing quote */
    *o = 0;
    return out;
}

static jnode *value(parser *ps);

static jnode *container(parser *ps, jtype t) {
    char close = t == J_OBJ ? '}' : ']';
    jnode *n = node(t), **tail = &n->child;
    ps->p++;
    ws(ps);
    if (*ps->p == close) { ps->p++; return n; }
    for (;;) {
        char *key = NULL;
        jnode *v;
        ws(ps);
        if (t == J_OBJ) {
            if (*ps->p != '"') { ps->err = "expected key"; break; }
            if (!(key = string(ps))) break;
            ws(ps);
            if (*ps->p != ':') { ps->err = "expected ':'"; free(key); break; }
            ps->p++;
        }
        if (!(v = value(ps))) { free(key); break; }
        v->key = key;
        *tail = v;
        tail = &v->next;
        ws(ps);
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == close) { ps->p++; return n; }
        ps->err = t == J_OBJ ? "expected ',' or '}'" : "expected ',' or ']'";
        break;
    }
    json_free(n);
    return NULL;
}

static jnode *value(parser *ps) {
    jnode *n;
    ws(ps);
    switch (*ps->p) {
    case '{': return container(ps, J_OBJ);
    case '[': return container(ps, J_ARR);
    case '"':
        n = node(J_STR);
        if (!(n->str = string(ps))) { free(n); return NULL; }
        return n;
    }
    if (!strncmp(ps->p, "null", 4)) { ps->p += 4; return node(J_NULL); }
    if (!strncmp(ps->p, "true", 4)) { ps->p += 4; n = node(J_BOOL); n->num = 1; return n; }
    if (!strncmp(ps->p, "false", 5)) { ps->p += 5; return node(J_BOOL); }
    if (*ps->p == '-' || (*ps->p >= '0' && *ps->p <= '9')) {
        char *end;
        n = node(J_NUM);
        n->num = strtod(ps->p, &end);
        ps->p = end;
        return n;
    }
    ps->err = "unexpected character";
    return NULL;
}

jnode *json_parse(const char *text, char *err, int errlen) {
    parser ps;
    jnode *n;
    ps.p = text;
    ps.err = NULL;
    n = value(&ps);
    if (n) {
        ws(&ps);
        if (*ps.p) { ps.err = "trailing data"; json_free(n); n = NULL; }
    }
    if (!n && err) {
        int line = 1;
        const char *q;
        for (q = text; q < ps.p; q++) line += *q == '\n';
        snprintf(err, (size_t)errlen, "line %d: %s", line, ps.err ? ps.err : "out of memory");
    }
    return n;
}

jnode *json_load(const char *path, char *err, int errlen) {
    FILE *f = fopen(path, "rb");
    char *buf;
    long len;
    jnode *n;
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
    n = json_parse(buf, err, errlen);
    free(buf);
    return n;
}

jnode *json_get(const jnode *obj, const char *key) {
    jnode *c;
    if (!obj || obj->type != J_OBJ) return NULL;
    for (c = obj->child; c; c = c->next)
        if (!strcmp(c->key, key)) return c;
    return NULL;
}

void json_free(jnode *n) {
    while (n) {
        jnode *next = n->next;
        json_free(n->child);
        free(n->key);
        free(n->str);
        free(n);
        n = next;
    }
}
