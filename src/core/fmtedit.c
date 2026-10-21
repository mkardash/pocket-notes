/* Formatted view editing: candidate texts checked by parsing them. */
#include "fmtedit.h"
#include "fmtview.h"
#include "mdstyle.h"
#include <stdlib.h>
#include <string.h>

int fe_parse_count = 0;

static int is_space(char c) { return c == ' ' || c == '\t'; }
static int is_cont(char c) { return ((unsigned char)c & 0xC0) == 0x80; }

/* Makes room for n bytes plus a NUL in a growable buffer. */
static void reserve(char **buf, int *cap, int n) {
    if (*cap > n) return;
    *cap = n + n / 2 + 64;
    *buf = realloc(*buf, *cap);
}

void fe_free(FeOut *o) {
    free(o->s);
    free(o->s2);
    memset(o, 0, sizeof *o);
}

/* ---- Parsed paragraphs ---- */

/* A paragraph parsed for the formatted view: styles, hidden bytes and content start. */
typedef struct {
    const char *s;
    int len, start;
    ParaStyle ps;
    unsigned char *hid;
    int hcap;
} Para;

static void para_parse(Para *p, const char *s, int len, int in_code) {
    fe_parse_count++;
    md_parse(s, len, in_code, &p->ps);
    if (p->hcap < len + 1) {
        p->hcap = len + len / 2 + 64;
        p->hid = realloc(p->hid, p->hcap);
    }
    fv_hidden(s, len, &p->ps, p->hid);
    p->s = s;
    p->len = len;
    p->start = fv_content_start(&p->ps);
}

static void para_free(Para *p) {
    md_free(&p->ps);
    free(p->hid);
    memset(p, 0, sizeof *p);
}

/* ---- What the formatted view shows ---- */

/* Block type (empty counts as normal), the visible content bytes and each one's
 * inline style (FV_INLINE | ST_LINK bits). */
typedef struct {
    BlockType type;
    int n, cap;
    char *b;
    unsigned char *st;
} Render;

static void render_reserve(Render *r, int n) {
    if (r->cap >= n && r->b) return;
    r->cap = n + n / 2 + 64;
    r->b = realloc(r->b, r->cap);
    r->st = realloc(r->st, r->cap);
}

static void render_free(Render *r) {
    free(r->b);
    free(r->st);
    memset(r, 0, sizeof *r);
}

static void render_of(const Para *p, Render *r) {
    render_reserve(r, p->len);
    r->type = p->ps.type == BLK_EMPTY ? BLK_NORMAL : p->ps.type;
    r->n = 0;
    for (int i = p->start; i < p->len; i++) {
        if (p->hid[i]) continue;
        r->b[r->n] = p->s[i];
        r->st[r->n++] = p->ps.style[i] & (FV_INLINE | ST_LINK);
    }
}

/* Index in the render of offset off: the visible content bytes before it. */
static int render_index(const Para *p, int off) {
    int v = 0;
    for (int i = p->start; i < off; i++) v += !p->hid[i];
    return v;
}

/* Inserts t[0..tlen) at index at, every new byte with style st. */
static void render_insert(Render *r, int at, const char *t, int tlen, int st) {
    render_reserve(r, r->n + tlen);
    memmove(r->b + at + tlen, r->b + at, r->n - at);
    memmove(r->st + at + tlen, r->st + at, r->n - at);
    memcpy(r->b + at, t, tlen);
    memset(r->st + at, st, tlen);
    r->n += tlen;
}

/* How a candidate's render may differ from the expected one. Outside these allowances the
 * block type, the visible bytes and every style must be equal. */
typedef struct {
    int new_at, new_len;    /* the new text's range in the expected render */
    int new_ws_any;         /* whitespace of the new text may have any style */
    int new_any;            /* the new text may have any style */
    int gain_at, gain_len;  /* range whose bytes may gain gain_bits */
    int gain_bits;
} Check;

static int render_match(const Render *e, const Render *g, const Check *ck) {
    if (e->type != g->type || e->n != g->n || memcmp(e->b, g->b, e->n) != 0) return 0;
    for (int i = 0; i < e->n; i++) {
        int es = e->st[i], gs = g->st[i];
        if (es == gs) continue;
        if (i >= ck->new_at && i < ck->new_at + ck->new_len &&
            (ck->new_any || (ck->new_ws_any && is_space(e->b[i]))))
            continue;
        if (i >= ck->gain_at && i < ck->gain_at + ck->gain_len &&
            (gs & es) == es && (gs & ~es & ~ck->gain_bits) == 0)
            continue;
        return 0;
    }
    return 1;
}

/* Old byte i is new byte i before at, is gone in [at, at + del), and is i - del + ins after. */
typedef struct { int at, del, ins; } Splice;

/* Typing may add formatting, never remove it: every hidden old byte stays hidden and every
 * visible old content byte keeps its styles (it may gain more). The block type may change. */
static int monotonic(const Para *a, const Para *b, Splice m) {
    for (int i = 0; i < a->len; i++) {
        int j;
        if (i < m.at) j = i;
        else if (i < m.at + m.del) continue;
        else j = i - m.del + m.ins;
        if (a->hid[i]) {
            if (!b->hid[j]) return 0;
        } else if (i >= a->start && (a->ps.style[i] & (FV_INLINE | ST_LINK) & ~b->ps.style[j])) {
            return 0;
        }
    }
    return 1;
}

/* ---- Typing ---- */

/* One typing edit: the old paragraph, the cursor spot and what is typed there. */
typedef struct {
    const char *s;
    int len, in_code;
    const char *t;
    int tlen, want;
    int off;        /* canonical cursor */
    int h;          /* end of the hidden run starting at off */
    int j, k;       /* move-closer: hidden run s[j..k) before whitespace s[k..off); none if j == k */
    int v;          /* render index of the cursor */
    unsigned char *inner;   /* [0..len]: spot inside a link destination or footnote label */
    Para old, cur;
    Render exp, got;
    FeOut *o;
} Ins;

/* Candidate kinds. */
enum { K_PLAIN, K_MOVE, K_WRAP, K_ESCAPE };

/* A candidate: kind, insertion offset c and the text it is built from. Evaluation fills in
 * its canonical cursor. */
typedef struct { int kind, c; const char *base; int cursor; } Cand;

/* Checks a candidate passed. */
enum { P_A = 1, P_B = 2, P_C = 4, P_MONO = 8 };

/* Earlier results kept for the later tiers. */
typedef struct {
    Cand b, c, plain;   /* first tier-B pass, first tier-C pass, plain insert at the cursor */
    int has_b, has_c, plain_mono;
} Tally;

static const struct { int bit; const char *mk; } MARKS[] = {
    { ST_BOLD, "**" }, { ST_ITALIC, "*" }, { ST_STRIKE, "~~" }, { ST_HL, "==" }, { ST_CODE, "`" },
};
#define NMARKS ((int)(sizeof MARKS / sizeof MARKS[0]))

/* Writes the opening markers for bits (BOLD, ITALIC, STRIKE, HL, CODE); returns the length. */
static int put_open(char *d, int bits) {
    int n = 0;
    for (int i = 0; i < NMARKS; i++) {
        if (!(bits & MARKS[i].bit)) continue;
        int l = (int)strlen(MARKS[i].mk);
        memcpy(d + n, MARKS[i].mk, l);
        n += l;
    }
    return n;
}

/* Writes the closing markers for bits, in the reverse order; returns the length. */
static int put_close(char *d, int bits) {
    int n = 0;
    for (int i = NMARKS - 1; i >= 0; i--) {
        if (!(bits & MARKS[i].bit)) continue;
        int l = (int)strlen(MARKS[i].mk);
        memcpy(d + n, MARKS[i].mk, l);
        n += l;
    }
    return n;
}

/* Marks inner[c] for each spot inside a link destination or footnote label: from just after
 * its ']' (link) or '[' (footnote) up to its closing bracket. Text typed there would change
 * what the user cannot see, so those spots are never candidates and '_' there is not emphasis. */
static void find_inner(Ins *x) {
    const char *s = x->s;
    const unsigned char *hid = x->old.hid;
    x->inner = calloc(x->len + 1, 1);
    for (int i = 0; i + 1 < x->len; i++) {
        char close = 0;
        if (!hid[i] || !hid[i + 1]) continue;
        if (s[i] == ']' && s[i + 1] == '(') close = ')';
        else if (s[i] == '[' && s[i + 1] == '^') close = ']';
        if (!close) continue;
        int e = i + 2;
        while (e < x->len && s[e] != close) e++;
        if (e >= x->len) continue;
        memset(x->inner + i + 1, 1, e - i);
        i = e;
    }
}

/* Can typed text go in at spot c of the hidden run? Never inside a UTF-8 character or a link
 * destination; the cursor spot itself always can. */
static int spot_ok(const Ins *x, int c) {
    return c == x->off || (!(c < x->len && is_cont(x->s[c])) && !x->inner[c]);
}

/* Inline style the text has at offset c: that of the visible character before it. */
static int style_at(const Ins *x, int c) {
    return fv_style_left(x->s, x->len, &x->old.ps, x->old.hid, c);
}

/* Writes the candidate's text to o->s. Returns the offset right after the typed text. */
static int cand_build(Ins *x, const Cand *cd) {
    FeOut *o = x->o;
    const char *b = cd->base;
    int len = x->len, tlen = x->tlen, n = 0, raw;
    reserve(&o->s, &o->cap, len + tlen + 40);
    char *d = o->s;
    if (cd->kind == K_MOVE) {
        /* b[0:j] + b[k:off] + t + b[j:k] + b[off:] */
        memcpy(d, b, x->j);                         n = x->j;
        memcpy(d + n, b + x->k, x->off - x->k);     n += x->off - x->k;
        memcpy(d + n, x->t, tlen);                  n += tlen;
        raw = n;
        memcpy(d + n, b + x->j, x->k - x->j);       n += x->k - x->j;
        memcpy(d + n, b + x->off, len - x->off);    n += len - x->off;
    } else {
        /* b[0:c] + pre + t + post + b[c:] */
        char pre[24], post[24];
        int np = 0, nq = 0, c = cd->c;
        if (cd->kind == K_WRAP) {
            int S = style_at(x, c), on = x->want & ~S, off = S & ~x->want;
            np = put_close(pre, off);
            np += put_open(pre + np, on);
            nq = put_close(post, on);
            nq += put_open(post + nq, off);
        } else if (cd->kind == K_ESCAPE) {
            pre[np++] = '\\';
        }
        memcpy(d, b, c);                n = c;
        memcpy(d + n, pre, np);         n += np;
        memcpy(d + n, x->t, tlen);      n += tlen;
        raw = n;
        memcpy(d + n, post, nq);        n += nq;
        memcpy(d + n, b + c, len - c);  n += len - c;
    }
    d[n] = 0;
    o->len = n;
    return raw;
}

/* Are the bytes [from, to) all visible? */
static int all_visible(const Para *p, int from, int to) {
    for (int i = from; i < to; i++)
        if (p->hid[i]) return 0;
    return 1;
}

/* Builds and parses a candidate, sets its cursor and returns the checks it passes. Once it
 * passes tier A the later checks are skipped. */
static int cand_eval(Ins *x, Cand *cd) {
    int raw = cand_build(x, cd), f = 0;
    para_parse(&x->cur, x->o->s, x->o->len, x->in_code);
    cd->cursor = fv_snap(x->cur.s, x->cur.len, &x->cur.ps, x->cur.hid, raw);
    /* Tiers A to C also need every typed byte visible: otherwise the parser has taken it as
     * markup (an old marker showing in its place) and the cursor would land before it. */
    if (cd->kind != K_ESCAPE && all_visible(&x->cur, raw - x->tlen, raw)) {
        Check ck = { x->v, x->tlen, 0, 0, 0, 0, 0 };
        if (cd->kind == K_MOVE) {
            /* The moved whitespace may take on the typing style. */
            ck.gain_len = x->off - x->k;
            ck.gain_at = x->v - ck.gain_len;
            ck.gain_bits = x->want;
        }
        render_of(&x->cur, &x->got);
        if (render_match(&x->exp, &x->got, &ck)) return P_A | P_B;
        ck.new_ws_any = 1;
        if (render_match(&x->exp, &x->got, &ck)) f |= P_B;
        ck.new_ws_any = 0;
        ck.new_any = 1;
        if (cd->kind == K_PLAIN && cd->base == x->s && render_match(&x->exp, &x->got, &ck)) f |= P_C;
    }
    if ((cd->kind == K_PLAIN || cd->kind == K_ESCAPE) && cd->c == x->off) {
        Splice m = { x->off, 0, x->cur.len - x->len };
        if (monotonic(&x->old, &x->cur, m)) f |= P_MONO;
    }
    return f;
}

/* Evaluates a candidate. Returns 1 if it passes tier A (o then holds it); otherwise keeps
 * what the later tiers need in tl. */
static int consider(Ins *x, Cand cd, Tally *tl) {
    int f = cand_eval(x, &cd);
    if (f & P_A) {
        x->o->cursor = cd.cursor;
        return 1;
    }
    if ((f & P_B) && !tl->has_b) { tl->b = cd; tl->has_b = 1; }
    if ((f & P_C) && !tl->has_c) { tl->c = cd; tl->has_c = 1; }
    if (cd.kind == K_PLAIN && cd.c == x->off) { tl->plain = cd; tl->plain_mono = (f & P_MONO) != 0; }
    return 0;
}

/* Tier A over the candidates built from base, in order: plain insert at each spot of the
 * hidden run, move-closer, wrap at each spot. Returns 1 when one passes. Tier B (same
 * candidates) and tier C (plain inserts into s) passes are kept in tl. */
static int try_tier_a(Ins *x, const char *base, Tally *tl) {
    /* Plain insert at each spot c = off..h. */
    for (int c = x->off; c <= x->h; c++)
        if (spot_ok(x, c) && consider(x, (Cand){ K_PLAIN, c, base, 0 }, tl))
            return 1;
    /* Move-closer: the hidden run before the whitespace moves past the new text. */
    if (x->j < x->k && consider(x, (Cand){ K_MOVE, x->off, base, 0 }, tl)) return 1;
    /* Wrap: markers around the new text for the style bits that differ (same as plain if none). */
    for (int c = x->off; c <= x->h; c++)
        if (spot_ok(x, c) && style_at(x, c) != x->want &&
            consider(x, (Cand){ K_WRAP, c, base, 0 }, tl))
            return 1;
    return 0;
}

/* Writes an earlier candidate to o again and takes its cursor. */
static void accept(Ins *x, const Cand *cd) {
    cand_build(x, cd);
    x->o->cursor = cd->cursor;
}

/* Copies s to o->s2 with each hidden '_' emphasis marker turned into '*' (it looks the same).
 * Underscores in link destinations and footnote labels are not markers and stay. Returns how
 * many changed. */
static int underscore_text(Ins *x) {
    int n = 0;
    if (fv_para_hidden(&x->old.ps)) return 0;
    reserve(&x->o->s2, &x->o->cap2, x->len);
    char *d = x->o->s2;
    for (int i = 0; i < x->len; i++) {
        d[i] = x->s[i];
        if (d[i] == '_' && x->old.hid[i] && !x->inner[i]) {
            d[i] = '*';
            n++;
        }
    }
    d[x->len] = 0;
    return n;
}

/* Runs the tiers in their fixed order and leaves the accepted text and cursor in o. */
static void insert_tiers(Ins *x) {
    Tally t1 = {0}, t2 = {0};
    int nus;

    /* Tier A (exact), then tier B (relaxed: typed whitespace may have any style). */
    if (try_tier_a(x, x->s, &t1)) return;
    if (t1.has_b) { accept(x, &t1.b); return; }

    /* Tiers A' and B': the same on the text with '_' emphasis markers rewritten as '*'. */
    nus = underscore_text(x);
    if (nus) {
        if (try_tier_a(x, x->o->s2, &t2)) return;
        if (t2.has_b) { accept(x, &t2.b); return; }
    }

    /* Tier C (visible only): the visible text is right and old characters keep their styles. */
    if (t1.has_c) { accept(x, &t1.c); return; }

    /* Tier D (AutoFormat): the plain insert, if it only adds formatting. */
    if (t1.plain_mono) { accept(x, &t1.plain); return; }

    /* Tier E (escape): a typed punctuation character goes in as "\c". */
    if (x->tlen == 1 && md_is_escapable(x->t[0])) {
        Cand e = { K_ESCAPE, x->off, x->s, 0 };
        if (cand_eval(x, &e) & P_MONO) { x->o->cursor = e.cursor; return; }
    }

    /* Tier F (underscore): '_' markers rewritten as '*', then the plain insert. */
    if (nus && t2.plain_mono) { accept(x, &t2.plain); return; }

    /* Tier G (last resort): the plain insert. */
    accept(x, &t1.plain);
}

void fe_insert(const char *s, int len, int in_code, int off, const char *t, int tlen, int want, FeOut *o) {
    Ins x;
    memset(&x, 0, sizeof x);
    x.s = s;
    x.len = len;
    x.in_code = in_code;
    x.t = t;
    x.tlen = tlen < 0 ? 0 : tlen;
    x.want = want & FV_INLINE;
    x.o = o;
    o->len2 = 0;

    para_parse(&x.old, s, len, in_code);
    x.off = fv_snap(s, len, &x.old.ps, x.old.hid, off);
    /* The hidden run at the cursor, and the whitespace and hidden run left of it. */
    for (x.h = x.off; x.h < len && x.old.hid[x.h]; x.h++) {}
    find_inner(&x);
    x.k = x.off;
    while (x.k > x.old.start && is_space(s[x.k - 1]) && !x.old.hid[x.k - 1]) x.k--;
    x.j = x.k;
    if (x.k < x.off)
        while (x.j > x.old.start && x.old.hid[x.j - 1]) x.j--;

    /* Expected render: the old one with t at the cursor, styled want. */
    render_of(&x.old, &x.exp);
    x.v = render_index(&x.old, x.off);
    render_insert(&x.exp, x.v, t, x.tlen, x.want);

    insert_tiers(&x);

    free(x.inner);
    para_free(&x.old);
    para_free(&x.cur);
    render_free(&x.exp);
    render_free(&x.got);
}
