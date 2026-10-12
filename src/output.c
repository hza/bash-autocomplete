/* Candidate queue: emit() escapes, filters and trims; flush() folds
 * (vars, flags, groups, snippets) and prints the final list. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "complete.h"
#include "output.h"
#include "spec.h"
#include "util.h"

const char *g_cur;   /* current word, unescaped (for filtering) */
const char *g_raw;   /* current word as typed (for wordbreak trimming) */
const char *g_wbreaks;
char *g_qpre;        /* inside an open quote: text after the quote up to the
                             * last space; candidates become g_qpre + word, unescaped */

/* Candidates are collected and printed by flush() once completion is done,
 * so the output can depend on how many matched. */
#define MAXCANDS 4096
typedef struct {
    char *text;       /* escaped, trimmed to bash's current word */
    int space;        /* finished word: append a space */
    const char *desc; /* optional description (spec "description") */
    const char *kind; /* source, e.g. "target"; tagged when kinds mix */
    int show;         /* flag "show": SHOW_DEFAULT, SHOW_ALWAYS (never folded), SHOW_COLLAPSED (only in 📌) */
    const char *group; /* key in "groups" ("Up"): collapsed together */
    const char *gdesc; /* that group's description */
} cand;
static cand g_cands[MAXCANDS];
static int g_ncands;

int ncands(void) { return g_ncands; }
const char *g_kind; /* kind given to candidates emitted now */
int g_show;         /* "show" given to candidates emitted now */
int g_noesc;        /* display-only candidates: no shell escaping */
int g_verbatim;     /* snippets: no filter, escaping or trimming */
int g_nofilter;     /* exec `filter: false`: the command already matched g_cur */
const char *g_group, *g_gdesc; /* group (and its description) given to candidates emitted now */

/* Escape for bash and queue, trimmed to bash's current word. Duplicates
 * (union values can overlap) are dropped, keeping the first description. */
void emit_desc(const char *cand, int space, const char *desc) {
    char esc[4096];
    size_t n = 0;
    const char *p;
    if (g_verbatim) { /* caller filtered; text replaces the whole word */
        p = cand;
        goto queue;
    }
    if (!g_nofilter && strncmp(cand, g_cur, strlen(g_cur)) != 0) return;
    if (g_qpre) {
        /* skip words already inside the quote */
        const char *w = g_qpre;
        size_t cl = strlen(cand);
        while ((w = strstr(w, cand)))
            if ((w == g_qpre || w[-1] == ' ') && w[cl] == ' ') return;
            else w++;
        /* bash replaces everything after the open quote; no escaping. No
         * space: readline closes the quote after a single match */
        snprintf(esc, sizeof esc, "%s%s", g_qpre, cand);
        space = 0;
        p = esc;
        goto queue;
    }
    for (p = cand; *p && n < sizeof esc - 3; p++) {
        /* leading ~ stays bare: it matches the typed word and keeps tilde expansion */
        if (!g_noesc && strchr(" \t\\'\"()<>&;|*?[]$`!#{}~", *p) && !(*p == '~' && p == cand))
            esc[n++] = '\\';
        esc[n++] = *p;
    }
    esc[n] = 0;

    /* bash splits words at COMP_WORDBREAKS; the reply must replace only the
     * part after the last break char of the raw word. */
    p = esc;
    {
        const char *r = g_raw, *last = NULL;
        for (; *r; r++)
            if (strchr(g_wbreaks, *r)) last = r;
        if (last) {
            size_t k = (size_t)(last - g_raw) + 1;
            if (strncmp(esc, g_raw, k) == 0) p = esc + k;
        }
    }
queue:
    {
        int i;
        for (i = 0; i < g_ncands; i++)
            if (!strcmp(g_cands[i].text, p)) {
                if (!g_cands[i].desc && desc) g_cands[i].desc = strdup(desc);
                return;
            }
    }
    if (g_ncands < MAXCANDS) {
        g_cands[g_ncands].text = strdup(p);
        g_cands[g_ncands].space = space;
        g_cands[g_ncands].kind = g_kind;
        g_cands[g_ncands].show = g_show;
        g_cands[g_ncands].group = g_group;
        g_cands[g_ncands].gdesc = g_gdesc;
        g_cands[g_ncands++].desc = desc ? strdup(desc) : NULL;
    }
}

void emit(const char *cand, int space) { emit_desc(cand, space, NULL); }

/* Print queued candidates. With several matches and any description, each
 * candidate becomes "name  -- description", padded so bash lists one per
 * line. When candidates of different kinds mix (make targets and variables),
 * the description starts with a "[kind]" tag, since bash sorts the list and
 * real section headers are impossible. bash only inserts the common prefix of ambiguous matches, and names
 * differ before the padding starts, so descriptions never reach the command
 * line. A single match is always printed bare so it can be inserted. */
/* On an empty word, make variables are folded into one display-only
 * "📌 vars" entry (description: their names) so the list shows targets
 * first; typing a letter lists matching ones. It is never inserted: other
 * candidates remain and share no prefix with it. Kept unfolded when there
 * are no targets to show. */
static int is_var(const cand *c) { return c->kind && !strcmp(c->kind, "var"); }

static void collapse_vars(void) {
    char names[1024] = "";
    int i, j, hidden = 0;
    size_t have = 0;
    if (*g_cur) return;
    for (i = 0; i < g_ncands; i++)
        if (is_var(&g_cands[i])) {
            const char *t = g_cands[i].text;
            hidden++;
            if (have < sizeof names - 1)
                have += (size_t)snprintf(names + have, sizeof names - have, "%s%.*s", have ? " " : "",
                                         (int)strcspn(t, "="), t);
        }
    if (!hidden || hidden == g_ncands) return;
    for (i = j = 0; i < g_ncands; i++)
        if (!is_var(&g_cands[i])) g_cands[j++] = g_cands[i];
    g_ncands = j;
    g_kind = "target"; /* sits among targets: no [kind] tags */
    /* U+00A0 no-break space: a plain one would be shown escaped ("\ ") */
    emit_desc("📌 vars:", 0, strdup(names));
}

/* Flags are folded into display-only "<word>📌" entries:
 * - "show: collapsed" flags always: short ones into "<word>📌<letters>",
 *   one entry per kind of letter ("-📌 0123" for digits, "-📌 MP" for
 *   letters, then other symbols), description "(1) desc (2) desc"; long
 *   ones into "<word>📌" ("N more: names" when more than 5);
 * - on a bare "-", the long "--x" flags (unless "show: always") when there
 *   are 15 or more, into "-📌 --" ("N long options: type -- to list"), so
 *   the short flags stand out. Collapsed long flags go there too.
 * Nothing is folded when nothing else would be listed. "📌" sorts after
 * ASCII, so they are listed last. Never inserted: other candidates remain
 * and share only the typed word with them. */
static int is_long_fold(const cand *c, int longs) {
    return longs && c->show != SHOW_ALWAYS && !strncmp(c->text, "--", 2);
}

static int cmp_cand(const void *a, const void *b) {
    return strcmp(((const cand *)a)->text, ((const cand *)b)->text);
}

/* 0 digit, 1 upper, 2 lower, 3 other short flag, 4 long flag */
static int coll_class(const cand *c) {
    unsigned char ch = (unsigned char)c->text[1];
    if (ch == '-') return 4;
    return isdigit(ch) ? 0 : isupper(ch) ? 1 : islower(ch) ? 2 : 3;
}

/* Letter of short flag candidate c; its text is shell-escaped ("-\\#"). */
static char flag_char(const cand *c) { return c->text[c->text[1] == '\\' ? 2 : 1]; }

/* Is c in the entry for group grp (NULL: ungrouped flags of class cl)? */
static int coll_member(const cand *c, const char *grp, int cl) {
    if (grp) return c->group && !strcmp(c->group, grp);
    return !c->group && coll_class(c) == cl;
}

/* Emit "<word>📌 <letters>" for the short collapsed flags of group grp
 * (or, ungrouped, of class cl), starting a new entry whenever the
 * description would pass `room`. */
static void emit_short_coll(const cand *coll, int k, const char *grp, int cl, int room) {
    char desc[1024], text[256], item[512];
    size_t have = 0, nt = 0;
    int i;
    if (grp) { /* letters and descriptions in the order of the key */
        const char *p;
        const char *gd = NULL;
        nt = (size_t)snprintf(text, sizeof text, "%s📌 ", g_cur);
        desc[0] = 0;
        for (p = grp; *p; p++)
            for (i = 0; i < k; i++) {
                if (!coll_member(&coll[i], grp, cl) || flag_char(&coll[i]) != *p) continue;
                if (!gd && (gd = coll[i].gdesc)) have = (size_t)snprintf(desc, sizeof desc, "%s:", gd);
                if (nt < sizeof text - 1) text[nt++] = *p, text[nt] = 0;
                if (have < sizeof desc - 1)
                    have += (size_t)snprintf(desc + have, sizeof desc - have, "%s(%c) %s", have ? " " : "", *p,
                                             coll[i].desc ? coll[i].desc : "");
            }
        if (have) emit_desc(text, 0, desc);
        return;
    }
    for (i = 0; i <= k; i++) {
        size_t il = 0;
        if (i < k) {
            if (!coll_member(&coll[i], grp, cl)) continue;
            il = (size_t)snprintf(item, sizeof item, "(%c) %s", flag_char(&coll[i]), coll[i].desc ? coll[i].desc : "");
        }
        if (have && (i == k || (int)(have + 1 + il) > room)) {
            emit_desc(text, 0, desc);
            have = 0;
        }
        if (i == k) break;
        if (!have) {
            nt = (size_t)snprintf(text, sizeof text, "%s📌\u00a0", g_cur);
            desc[0] = 0;
        }
        if (nt < sizeof text - 1) text[nt++] = flag_char(&coll[i]), text[nt] = 0;
        if (have < sizeof desc - 1)
            have += (size_t)snprintf(desc + have, sizeof desc - have, "%s%s", have ? " " : "", item);
    }
}

/* "show: collapsed" hides short flags only; long ones are listed (and
 * grouped by letter) as usual, except in the "-📌" long-options fold. */
static int is_coll(const cand *c) { return c->show == SHOW_COLLAPSED && c->text[1] != '-'; }

static void collapse_flags(void) {
    static cand coll[MAXCANDS];
    char desc[1024], text[256];
    const char *kind = NULL;
    int i, j, k = 0, cl, nlong = 0, ncoll = 0, longs = 0;
    if (g_cur[0] != '-') return;
    if (!strcmp(g_cur, "-")) {
        for (i = 0; i < g_ncands; i++)
            if (is_long_fold(&g_cands[i], 1)) longs++;
        longs = longs >= 15;
    }
    for (i = 0; i < g_ncands; i++) {
        if (is_long_fold(&g_cands[i], longs)) nlong++;
        else if (is_coll(&g_cands[i])) ncoll++;
        else continue;
        kind = g_cands[i].kind;
    }
    if (!nlong && !ncoll) return;
    if (nlong + ncoll == g_ncands) return;
    for (i = j = 0; i < g_ncands; i++) {
        if (is_long_fold(&g_cands[i], longs)) continue;
        if (is_coll(&g_cands[i])) coll[k++] = g_cands[i];
        else g_cands[j++] = g_cands[i];
    }
    g_ncands = j;
    qsort(coll, (size_t)k, sizeof *coll, cmp_cand); /* "-📌 123" */
    g_kind = kind;
    g_show = SHOW_ALWAYS;
    g_noesc = 1; /* never inserted: show "-📌 Z#N", not "Z\\#N" */
    { /* name column: widest remaining name (see flush) */
        int w = 12;
        for (i = 0; i < g_ncands; i++)
            if (dwidth(g_cands[i].text) > w) w = dwidth(g_cands[i].text);
        for (cl = 0; cl < 4; cl++) emit_short_coll(coll, k, NULL, cl, term_width() - w - 8);
    }
    for (i = 0; i < k; i++) { /* groups, each once */
        const char *g = coll[i].group;
        if (!g) continue;
        for (j = 0; j < i && !(coll[j].group && !strcmp(coll[j].group, g)); j++) {}
        if (j < i) continue;
        emit_short_coll(coll, k, g, 0, 0);
    }
    if (nlong) {
        snprintf(desc, sizeof desc, "%d long options: type -- to list", nlong);
        snprintf(text, sizeof text, "%s📌 --", g_cur);
        emit_desc(text, 0, desc);
    }
    g_show = SHOW_DEFAULT;
    g_noesc = 0;
}

/* A short flag listed next to a long one with the same description
 * replaces it, the long name becoming a hint: "-d  -- HTTP POST data
 * (🔥 --data)". Each long flag takes at most one short. */
static void merge_pairs(void) {
    static char paired[MAXCANDS];
    int i, j, k;
    if (g_cur[0] != '-') { /* empty word: only when all are flags (do_complete's fallback) */
        if (*g_cur) return;
        for (i = 0; i < g_ncands; i++)
            if (g_cands[i].text[0] != '-') return;
    }
    memset(paired, 0, (size_t)g_ncands);
    for (i = k = 0; i < g_ncands; i++) {
        cand *c = &g_cands[i];
        int hit = -1;
        if (c->text && c->text[1] && c->text[1] != '-' && c->desc && !strstr(c->text, "📌"))
            for (j = 0; j < g_ncands; j++)
                if (!paired[j] && g_cands[j].text && !strncmp(g_cands[j].text, "--", 2) && g_cands[j].desc &&
                    !strcmp(g_cands[j].desc, c->desc)) {
                    hit = j;
                    break;
                }
        if (hit < 0) continue;
        {
            cand *keep = c, *drop = &g_cands[hit]; /* the short stays, the long one goes */
            char *d = malloc(strlen(c->desc) + strlen(drop->text) + 16);
            if (!d) continue;
            sprintf(d, "%s (🔥 %s)", c->desc, drop->text);
            paired[hit] = 1;
            keep->desc = d;
            drop->text = NULL; /* dropped below */
        }
    }
    for (i = k = 0; i < g_ncands; i++)
        if (g_cands[i].text) g_cands[k++] = g_cands[i];
    g_ncands = k;
}

/* A flag list taller than the terminal is grouped by the next letter after
 * the typed word: "--content-disposition" and "--content-on-error" fall
 * into one "--c🧩" entry (no space, description: the member names, those
 * sharing a first word shown once as "word-🧩"), so typing the letter lists
 * that group. Then the smallest groups are expanded back while the list
 * still fits, so a group of two only stays folded when space is short. */

static size_t group_len(const char *t, size_t n) {
    const char *p = t + n;
    while (*p == '-') p++;
    return (size_t)(p - t) + (*p != 0);
}

static char *group_names(const int *gid, int g) {
    char names[1024] = "";
    size_t have = 0;
    int a, b;
    for (a = 0; a < g_ncands; a++) {
        const char *ma = g_cands[a].text, *mb;
        size_t w;
        int same = 0, first = 1;
        if (gid[a] != g) continue;
        while (*ma == '-') ma++;
        w = strcspn(ma, "-");
        /* names sharing a first word: "sasl-authzid sasl-ir" -> "sasl-🧩" */
        if (ma[w])
            for (b = 0; b < g_ncands; b++) {
                if (gid[b] != g) continue;
                for (mb = g_cands[b].text; *mb == '-'; mb++) {}
                if (!strncmp(mb, ma, w + 1)) {
                    same++;
                    if (b < a) first = 0;
                }
            }
        if (same >= 2 && !first) continue;
        if (have < sizeof names - 1)
            have += (size_t)snprintf(names + have, sizeof names - have, "%s%.*s%s", have ? " " : "",
                                     same >= 2 ? (int)w + 1 : (int)strlen(ma), ma, same >= 2 ? "🧩" : "");
    }
    return strdup(names);
}

static void group_flags(void) {
    static cand out[MAXCANDS];
    static int gid[MAXCANDS], head[MAXCANDS], size[MAXCANDS];
    size_t n = strlen(g_cur), len;
    int i, j, k = 0, ng = 0, lines, limit = term_rows() - 2;
    if (g_cur[0] != '-' || g_ncands <= limit) return;
    for (i = 0; i < g_ncands; i++) {
        gid[i] = -1;
        if (strncmp(g_cands[i].text, g_cur, n) || g_cands[i].show == SHOW_ALWAYS) continue;
        len = group_len(g_cands[i].text, n);
        for (j = 0; j < ng; j++)
            if (group_len(g_cands[head[j]].text, n) == len && !strncmp(g_cands[head[j]].text, g_cands[i].text, len))
                break;
        if (j == ng) { head[ng] = i; size[ng++] = 0; }
        gid[i] = j;
        size[j]++;
    }
    lines = g_ncands;
    for (j = 0; j < ng; j++)
        if (size[j] > 1) lines -= size[j] - 1;
    for (;;) { /* expand the smallest folded group that still fits */
        int best = -1;
        for (j = 0; j < ng; j++)
            if (size[j] > 1 && lines + size[j] - 1 <= limit && (best < 0 || size[j] < size[best])) best = j;
        if (best < 0) break;
        lines += size[best] - 1;
        size[best] = 1;
    }
    for (i = 0; i < g_ncands; i++) {
        j = gid[i];
        if (j < 0 || size[j] == 1) { out[k++] = g_cands[i]; continue; }
        if (head[j] != i) continue;
        out[k] = g_cands[i];
        { /* the entry is the members' common prefix + "🧩": "--no-🧩" for "--no-x --no-y";
           * display-only, since groups differ in their next letter bash never
           * inserts past it */
            size_t lcp = strlen(g_cands[i].text);
            int b;
            for (b = i + 1; b < g_ncands; b++)
                if (gid[b] == j) {
                    size_t c = 0;
                    while (c < lcp && g_cands[b].text[c] == g_cands[i].text[c]) c++;
                    lcp = c;
                }
            out[k].text = malloc(lcp + sizeof "🧩");
            snprintf(out[k].text, lcp + sizeof "🧩", "%.*s🧩", (int)lcp, g_cands[i].text);
        }
        out[k].space = 0;
        out[k++].desc = group_names(gid, j);
    }
    memcpy(g_cands, out, (size_t)k * sizeof *out);
    g_ncands = k;
}

/* On an empty word or "@", the spec's "snippets" ({key, command,
 * description?, mark?}) are listed as display-only "📌 @key" (mark: pin,
 * the most used ones) or "🔥 @key" entries described by
 * "command   📚 description". The two icons differ in the first byte, so
 * bash lists instead of inserting a common prefix (verified in pty); when
 * all entries are of one kind, one gets the other icon (the smallest key
 * is pinned, or the largest unpinned). 🔥 (U+1F525) sorts after 📌 (U+1F4CC).
 * "@key" typed in full is replaced by the snippet's command after
 * "<name> "; bash keeps a leading "@" in the replaced text (readline
 * special prefix, verified in pty). Snippets not starting with "<name> "
 * (pipelines) are only listed. An empty g_snippets_name (exec
 * `snippets: true` flag values) substitutes the whole value, single-quoted.
 * When the empty word has other candidates (lsof: files), snippets are
 * listed only on "@". */
const ynode *g_snippets;
const char *g_snippets_name;

static int pinned(const ynode *e) {
    const ynode *m = yaml_get(e, "mark");
    return m && m->type == Y_STR && !strcmp(m->str, "pin");
}

static void add_snippets(void) {
    const ynode *e;
    int want, npin = 0, nall = 0;
    char first = 0, last = 0;
    size_t nl;
    char name[32], desc[1024];
    if (!g_snippets || g_snippets->type != Y_ARR) return;
    want = g_cur[0] == '@' && g_cur[1] && !g_cur[2];
    if (*g_cur && strcmp(g_cur, "@") && !want) return;
    if (!*g_cur && g_ncands) return; /* other candidates (lsof files): snippets only on "@" */
    nl = strlen(g_snippets_name);
    g_kind = g_ncands ? g_cands[0].kind : NULL; /* no [kind] tags */
    g_verbatim = 1;
    for (e = g_snippets->child; e; e = e->next) {
        const ynode *k = yaml_get(e, "key");
        if (!snippet_key(k)) continue;
        nall++;
        npin += pinned(e);
        if (!first || k->str[0] < first) first = k->str[0];
        if (k->str[0] > last) last = k->str[0];
    }
    for (e = g_snippets->child; e; e = e->next) {
        const ynode *cmd = yaml_get(e, "command"), *k = yaml_get(e, "key");
        const char *d = spec_desc(e);
        int pin;
        if (!cmd || cmd->type != Y_STR || !snippet_key(k)) continue;
        if (want) {
            if (k->str[0] != g_cur[1]) continue;
            if (!nl) { /* flag value (exec snippets): quoted whole */
                char q[4096], *o = q;
                const char *c;
                *o++ = '\'';
                for (c = cmd->str; *c && o < q + sizeof q - 6; c++)
                    if (*c == '\'') { memcpy(o, "'\\''", 4); o += 4; } else *o++ = *c;
                *o++ = '\''; *o = 0;
                emit_desc(q, 1, NULL);
            } else if (!strncmp(cmd->str, g_snippets_name, nl) && cmd->str[nl] == ' ') {
                emit_desc(cmd->str + nl + 1, 1, NULL);
            } else { /* symlinked spec (fly -> flyctl): snippets use the real name */
                char alt[1024];
                size_t al = (size_t)snprintf(alt, sizeof alt, "%s%s", g_spec_real,
                                             g_snippets_name + strcspn(g_snippets_name, " "));
                if (al < sizeof alt && !strncmp(cmd->str, alt, al) && cmd->str[al] == ' ')
                    emit_desc(cmd->str + al + 1, 1, NULL);
            }
            break; /* not substitutable: list nothing */
        }
        pin = !npin ? k->str[0] == first : npin == nall ? k->str[0] != last : pinned(e);
        snprintf(name, sizeof name, "%s\xc2\xa0@%s", pin ? "📌" : "🔥", k->str);
        if (d) snprintf(desc, sizeof desc, "%s   📚 %s", cmd->str, d);
        emit_desc(name, 0, d ? desc : cmd->str);
    }
    g_verbatim = 0;
}

void flush(void) {
    int i, any = 0, width = 0, kwidth = 0, mixed = 0, cols, line;
    collapse_vars();
    add_snippets();
    collapse_flags();
    merge_pairs();
    group_flags();
    for (i = 0; i < g_ncands; i++) {
        int len = dwidth(g_cands[i].text);
        const char *k = g_cands[i].kind;
        if (g_cands[i].desc) any = 1;
        if (len > width) width = len;
        if (k && (int)strlen(k) + 2 > kwidth) kwidth = (int)strlen(k) + 2;
        if (i && (k ? !g_cands[0].kind || strcmp(k, g_cands[0].kind) : !!g_cands[0].kind)) mixed = 1;
    }
    if (mixed) any = 1;
    if (g_ncands < 2 || !any) {
        for (i = 0; i < g_ncands; i++)
            printf("%s%s\n", g_cands[i].text, g_cands[i].space ? " " : "");
        return;
    }
    cols = term_width();
    line = cols / 2 + 1; /* wider than half the screen: one per line */
    for (i = 0; i < g_ncands; i++) {
        char buf[4096];
        char tag[64] = "";
        int n;
        if (mixed && g_cands[i].kind) snprintf(tag, sizeof tag, "[%s]", g_cands[i].kind);
        if (mixed)
            n = snprintf(buf, sizeof buf, "%-*s  -- %-*s %s",
                         width + (int)strlen(g_cands[i].text) - dwidth(g_cands[i].text), g_cands[i].text, kwidth, tag,
                         g_cands[i].desc ? g_cands[i].desc : "");
        else if (g_cands[i].desc)
            n = snprintf(buf, sizeof buf, "%-*s  -- %s",
                         width + (int)strlen(g_cands[i].text) - dwidth(g_cands[i].text), g_cands[i].text, g_cands[i].desc);
        else
            n = snprintf(buf, sizeof buf, "%s", g_cands[i].text);
        if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
        buf[n] = 0;
        while (dwidth(buf) > cols - 2) { /* avoid wrapping; don't split a UTF-8 sequence */
            while (n > 0 && (buf[--n] & 0xC0) == 0x80) {}
            buf[n] = 0;
        }
        /* pad by display width: readline lays out columns by it, not bytes */
        printf("%s%*s\n", buf, line > dwidth(buf) ? line - dwidth(buf) : 0, "");
    }
}
