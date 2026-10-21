/* Formatted view editing: candidate texts checked by parsing them. */
#include "fmtedit.h"
#include "fmtview.h"
#include "mdstyle.h"
#include "utf8.h"
#include <stdlib.h>
#include <string.h>

int fe_parse_count = 0;

static int is_space(char c) { return c == ' ' || c == '\t'; }
static int is_cont(char c) { return ((unsigned char)c & 0xC0) == 0x80; }
static int is_marker(char c) { return c == '*' || c == '_' || c == '~' || c == '=' || c == '`'; }

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

/* A piece of a candidate text. */
typedef struct { const char *p; int n; } Piece;

/* Writes the pieces one after another to a growable buffer, NUL-terminated; sets *len. */
static void put_pieces(char **buf, int *cap, int *len, const Piece *pc, int np) {
    int n = 0;
    for (int i = 0; i < np; i++) n += pc[i].n;
    reserve(buf, cap, n);
    n = 0;
    for (int i = 0; i < np; i++) {
        if (pc[i].n) memcpy(*buf + n, pc[i].p, pc[i].n);
        n += pc[i].n;
    }
    (*buf)[n] = 0;
    *len = n;
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

/* Removes n bytes at index at. */
static void render_cut(Render *r, int at, int n) {
    memmove(r->b + at, r->b + at + n, r->n - at - n);
    memmove(r->st + at, r->st + at + n, r->n - at - n);
    r->n -= n;
}

/* Copies n bytes of r from index from to d, which gets block type type. */
static void render_slice(const Render *r, int from, int n, BlockType type, Render *d) {
    render_reserve(d, n);
    memcpy(d->b, r->b + from, n);
    memcpy(d->st, r->st + from, n);
    d->n = n;
    d->type = type;
}

/* How a candidate's render may differ from the expected one. Outside these allowances the
 * block type, the visible bytes and every style must be equal. */
typedef struct {
    int new_at, new_len;    /* the typed text's range (a delete's tier B: the whole render) */
    int new_ws_any;         /* whitespace in the new_ range may have any style */
    int new_any;            /* the new_ range may have any style */
    int flex_at, flex_len;  /* range whose bytes may gain gain_bits and lose lose_bits */
    int gain_bits, lose_bits;
} Check;

static int render_match(const Render *e, const Render *g, const Check *ck) {
    if (e->type != g->type || e->n != g->n || memcmp(e->b, g->b, e->n) != 0) return 0;
    for (int i = 0; i < e->n; i++) {
        int es = e->st[i], gs = g->st[i];
        if (es == gs) continue;
        if (i >= ck->new_at && i < ck->new_at + ck->new_len &&
            (ck->new_any || (ck->new_ws_any && is_space(e->b[i]))))
            continue;
        if (i >= ck->flex_at && i < ck->flex_at + ck->flex_len &&
            (gs & ~es & ~ck->gain_bits) == 0 && (es & ~gs & ~ck->lose_bits) == 0)
            continue;
        return 0;
    }
    return 1;
}

/* Old byte i is new byte i before at, is gone in [at, at + del), and is i - del + ins after. */
typedef struct { int at, del, ins; } Splice;

/* An edit may add formatting, never remove it: every hidden old byte stays hidden and every
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

/* ---- One edit ---- */

/* find_inner flags. */
enum {
    IN_DEST = 1,    /* spot inside a link destination or footnote label */
    IN_NOTE = 2     /* byte of a footnote reference "[^...]" */
};

/* Which candidates try_cands builds. */
enum { OP_INSERT, OP_DELETE };

/* One edit of a paragraph: the old text parsed, the render the edit should give, and the
 * buffers its candidates reuse. Candidate texts are written to o->s. */
typedef struct {
    const char *s;
    int len, in_code;
    Para old, cur;          /* s parsed; the candidate being checked */
    Render exp, exp2, got;  /* expected render (exp2: a split's right half); the candidate's */
    unsigned char *inner;   /* [0..len]: IN_DEST spots and IN_NOTE bytes */
    char *us;               /* s with its '_' emphasis markers turned into '*' */
    FeOut *o;
    int op;
    /* Typing */
    const char *t;
    int tlen, want;
    int off;        /* canonical cursor */
    int h;          /* end of the hidden run starting at off */
    int j, k;       /* move-closer: hidden run s[j..k) before whitespace s[k..off); none if j == k */
    int v;          /* render index of the cursor */
    /* Deleting, joining and restyling: the visible bytes [a, b) */
    int a, b;
    int p, n;       /* delete: the hidden runs s[p..a) and s[b..n) */
    const char *mk; /* restyle: the marker of the bit, mlen bytes */
    int mlen;
    Check ck;       /* restyle: how a candidate's render may differ from exp */
} Edit;

/* Marks inner[c] |= IN_DEST for each spot inside a link destination or footnote label: from
 * just after its ']' (link) or '[' (footnote) up to its closing bracket. Text typed there would
 * change what the user cannot see, so those spots are never candidates and '_' there is not
 * emphasis. Each byte of a footnote reference also gets IN_NOTE: no edit removes or copies it. */
static void find_inner(Edit *x) {
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
        for (int c = i + 1; c <= e; c++) x->inner[c] |= IN_DEST;
        if (close == ']')
            for (int c = i; c <= e; c++) x->inner[c] |= IN_NOTE;
        i = e;
    }
}

/* Can the text change at spot c? Never inside a UTF-8 character or a link destination; the
 * edit's own spot `self` always can. */
static int spot_ok(const Edit *x, int c, int self) {
    return c == self || (!(c < x->len && is_cont(x->s[c])) && !(x->inner[c] & IN_DEST));
}

/* Start of the hidden run that ends at i. It stays in the content and stops at a footnote. */
static int run_start(const Edit *x, int i) {
    while (i > x->old.start && x->old.hid[i - 1] && !(x->inner[i - 1] & IN_NOTE)) i--;
    return i;
}

/* End of the hidden run that starts at i. It stops at a footnote. */
static int run_end(const Edit *x, int i) {
    while (i < x->len && x->old.hid[i] && !(x->inner[i] & IN_NOTE)) i++;
    return i;
}

/* Start of the visible whitespace that ends at c (within the content). */
static int ws_start(const Edit *x, int c) {
    while (c > x->old.start && is_space(x->s[c - 1]) && !x->old.hid[c - 1]) c--;
    return c;
}

/* End of the visible whitespace that starts at c. */
static int ws_end(const Edit *x, int c) {
    while (c < x->len && is_space(x->s[c]) && !x->old.hid[c]) c++;
    return c;
}

/* The last visible content byte before off, or start - 1 when there is none. */
static int vis_before(const Edit *x, int off) {
    int i = (off > x->len ? x->len : off) - 1;
    while (i >= x->old.start && x->old.hid[i]) i--;
    return i < x->old.start ? x->old.start - 1 : i;
}

/* The first visible content byte at or after off, or len when there is none. */
static int vis_from(const Edit *x, int off) {
    int i = off < x->old.start ? x->old.start : off;
    while (i < x->len && x->old.hid[i]) i++;
    return i;
}

/* Fills x->us with s, each hidden '_' emphasis marker turned into '*' (it looks the same).
 * Underscores in link destinations and footnote labels are not markers and stay. Returns how
 * many changed. */
static int underscore_text(Edit *x) {
    int n = 0;
    if (fv_para_hidden(&x->old.ps)) return 0;
    if (!x->us) x->us = malloc(x->len + 1);
    for (int i = 0; i < x->len; i++) {
        x->us[i] = x->s[i];
        if (x->s[i] == '_' && x->old.hid[i] && !(x->inner[i] & IN_DEST)) {
            x->us[i] = '*';
            n++;
        }
    }
    x->us[x->len] = 0;
    return n;
}

/* Finishes setting up an edit once x->old holds its parsed text: exp starts as its render. */
static void edit_prepare(Edit *x) {
    find_inner(x);
    render_of(&x->old, &x->exp);
}

/* Sets up an edit of s whose candidates go to o. */
static void edit_open(Edit *x, const char *s, int len, int in_code, FeOut *o) {
    memset(x, 0, sizeof *x);
    x->s = s;
    x->len = len;
    x->in_code = in_code;
    x->o = o;
    para_parse(&x->old, s, len, in_code);
    edit_prepare(x);
}

static void edit_close(Edit *x) {
    free(x->inner);
    free(x->us);
    para_free(&x->old);
    para_free(&x->cur);
    render_free(&x->exp);
    render_free(&x->exp2);
    render_free(&x->got);
}

/* ---- Candidates ---- */

/* Candidate kinds. Typing: plain insert, move-closer, wrap and escape at spot c.
 * K_CUT: base without [c, d). K_BACK (move-closer back): the same cut, then the rest of the
 * hidden run after it, [d, n), moves in front of the whitespace before c.
 * K_MARKS: the restyle marker put in (or dropped) at spots c and d. */
enum { K_PLAIN, K_MOVE, K_WRAP, K_ESCAPE, K_CUT, K_BACK, K_MARKS };

/* K_MARKS flags: drop the marker that ends at c / starts at d instead of putting one in. */
enum { M_DROP_C = 1, M_DROP_D = 2 };

/* A candidate: kind, spots c and d, flags f and the text it is built from. Evaluation fills in
 * its canonical cursor. */
typedef struct { int kind, c, d, f; const char *base; int cursor; } Cand;

/* Checks a candidate passed. */
enum { P_A = 1, P_B = 2, P_C = 4, P_MONO = 8 };

/* Earlier results kept for the later tiers. */
typedef struct {
    Cand b, c, plain;   /* first tier-B pass, first tier-C pass, the plain edit */
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

/* Inline style the text has at offset c: that of the visible character before it. */
static int style_at(const Edit *x, int c) {
    return fv_style_left(x->s, x->len, &x->old.ps, x->old.hid, c);
}

/* Writes the candidate's text to o->s. Returns the raw cursor: the offset right after the typed
 * text (typing), the cut point (K_CUT) or the end of the word (K_MARKS). */
static int cand_build(Edit *x, const Cand *cd) {
    const char *b = cd->base;
    int len = x->len, c = cd->c, d = cd->d, np = 0, raw;
    char pre[24], post[24];
    Piece pc[5];
    if (cd->kind == K_MOVE) {
        /* b[0:j] + b[k:off] + t + b[j:k] + b[off:] */
        pc[np++] = (Piece){ b, x->j };
        pc[np++] = (Piece){ b + x->k, x->off - x->k };
        pc[np++] = (Piece){ x->t, x->tlen };
        pc[np++] = (Piece){ b + x->j, x->k - x->j };
        pc[np++] = (Piece){ b + x->off, len - x->off };
        raw = x->j + (x->off - x->k) + x->tlen;
    } else if (cd->kind == K_CUT) {
        /* b[0:c] + b[d:] */
        pc[np++] = (Piece){ b, c };
        pc[np++] = (Piece){ b + d, len - d };
        raw = c;
    } else if (cd->kind == K_BACK) {
        /* b[0:k] + b[d:n] + b[k:c] + b[n:], where b[k:c] is whitespace; the cursor goes after it */
        int k = ws_start(x, c);
        pc[np++] = (Piece){ b, k };
        pc[np++] = (Piece){ b + d, x->n - d };
        pc[np++] = (Piece){ b + k, c - k };
        pc[np++] = (Piece){ b + x->n, len - x->n };
        raw = c + x->n - d;
    } else if (cd->kind == K_MARKS) {
        /* b[0:c] + MK + b[c:d] + MK + b[d:], without the marker ending at c / starting at d if dropped */
        int m = x->mlen, dc = cd->f & M_DROP_C, dd = cd->f & M_DROP_D, e = dd ? d + m : d;
        pc[np++] = (Piece){ b, dc ? c - m : c };
        if (!dc) pc[np++] = (Piece){ x->mk, m };
        pc[np++] = (Piece){ b + c, d - c };
        if (!dd) pc[np++] = (Piece){ x->mk, m };
        pc[np++] = (Piece){ b + e, len - e };
        raw = x->b + (dc ? -m : m);
    } else {
        /* b[0:c] + pre + t + post + b[c:] */
        int npre = 0, npost = 0;
        if (cd->kind == K_WRAP) {
            int S = style_at(x, c), on = x->want & ~S, off = S & ~x->want;
            npre = put_close(pre, off);
            npre += put_open(pre + npre, on);
            npost = put_close(post, on);
            npost += put_open(post + npost, off);
        } else if (cd->kind == K_ESCAPE) {
            pre[npre++] = '\\';
        }
        pc[np++] = (Piece){ b, c };
        pc[np++] = (Piece){ pre, npre };
        pc[np++] = (Piece){ x->t, x->tlen };
        pc[np++] = (Piece){ post, npost };
        pc[np++] = (Piece){ b + c, len - c };
        raw = c + npre + x->tlen;
    }
    put_pieces(&x->o->s, &x->o->cap, &x->o->len, pc, np);
    return raw;
}

/* Are the bytes [from, to) all visible? */
static int all_visible(const Para *p, int from, int to) {
    for (int i = from; i < to; i++)
        if (p->hid[i]) return 0;
    return 1;
}

/* Is cd the plain edit: typing at the cursor, or cutting exactly [a, b)? */
static int is_plain(const Edit *x, const Cand *cd) {
    if (cd->kind == K_PLAIN) return cd->c == x->off;
    return cd->kind == K_CUT && cd->c == x->a && cd->d == x->b;
}

/* Typing checks on the parsed candidate. Once it passes tier A the later checks are skipped. */
static int insert_checks(Edit *x, const Cand *cd, int raw) {
    int f = 0;
    /* Tiers A to C also need every typed byte visible: otherwise the parser has taken it as
     * markup (an old marker showing in its place) and the cursor would land before it. */
    if (cd->kind != K_ESCAPE && all_visible(&x->cur, raw - x->tlen, raw)) {
        Check ck = { .new_at = x->v, .new_len = x->tlen };
        if (cd->kind == K_MOVE) {
            /* The moved whitespace may take on the typing style. */
            ck.flex_len = x->off - x->k;
            ck.flex_at = x->v - ck.flex_len;
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

/* Delete and join checks on the parsed candidate (a cut or a move-closer back): exact (tier A),
 * whitespace anywhere may have any style (tier B), and for the plain cut whether it only adds
 * formatting. */
static int cut_checks(Edit *x, const Cand *cd) {
    Check ck = {0};
    int f = 0;
    if (is_plain(x, cd)) {
        Splice m = { cd->c, cd->d - cd->c, 0 };
        if (monotonic(&x->old, &x->cur, m)) f |= P_MONO;
    }
    render_of(&x->cur, &x->got);
    if (render_match(&x->exp, &x->got, &ck)) return f | P_A | P_B;
    ck.new_len = x->exp.n;
    ck.new_ws_any = 1;
    if (render_match(&x->exp, &x->got, &ck)) f |= P_B;
    return f;
}

/* Builds and parses a candidate, sets its cursor and returns the checks it passes. */
static int cand_eval(Edit *x, Cand *cd) {
    int raw = cand_build(x, cd);
    para_parse(&x->cur, x->o->s, x->o->len, x->in_code);
    cd->cursor = fv_snap(x->cur.s, x->cur.len, &x->cur.ps, x->cur.hid, raw);
    if (cd->kind == K_CUT || cd->kind == K_BACK) return cut_checks(x, cd);
    if (cd->kind == K_MARKS) {
        render_of(&x->cur, &x->got);
        return render_match(&x->exp, &x->got, &x->ck) ? P_A : 0;
    }
    return insert_checks(x, cd, raw);
}

/* Evaluates a candidate. Returns 1 if it passes tier A (o then holds it); otherwise keeps
 * what the later tiers need in tl. */
static int consider(Edit *x, Cand cd, Tally *tl) {
    int f = cand_eval(x, &cd);
    if (f & P_A) {
        x->o->cursor = cd.cursor;
        return 1;
    }
    if ((f & P_B) && !tl->has_b) { tl->b = cd; tl->has_b = 1; }
    if ((f & P_C) && !tl->has_c) { tl->c = cd; tl->has_c = 1; }
    if (is_plain(x, &cd)) { tl->plain = cd; tl->plain_mono = (f & P_MONO) != 0; }
    return 0;
}

/* Writes an earlier candidate to o again and takes its cursor. */
static void accept(Edit *x, const Cand *cd) {
    cand_build(x, cd);
    x->o->cursor = cd->cursor;
}

/* Typing candidates built from base, in order: plain insert at each spot of the hidden run,
 * move-closer, wrap at each spot. Returns 1 when one passes tier A. */
static int try_inserts(Edit *x, const char *base, Tally *tl) {
    /* Plain insert at each spot c = off..h. */
    for (int c = x->off; c <= x->h; c++)
        if (spot_ok(x, c, x->off) && consider(x, (Cand){ .kind = K_PLAIN, .c = c, .base = base }, tl))
            return 1;
    /* Move-closer: the hidden run before the whitespace moves past the new text. */
    if (x->j < x->k && consider(x, (Cand){ .kind = K_MOVE, .c = x->off, .base = base }, tl)) return 1;
    /* Wrap: markers around the new text for the style bits that differ (same as plain if none). */
    for (int c = x->off; c <= x->h; c++)
        if (spot_ok(x, c, x->off) && style_at(x, c) != x->want &&
            consider(x, (Cand){ .kind = K_WRAP, .c = c, .base = base }, tl))
            return 1;
    return 0;
}

/* Can a move-closer-back candidate cut [c, d)? Only whitespace may be left between the run's
 * content and the rest of the hidden run after the cut, and something of that run must be left
 * to move. */
static int back_ok(const Edit *x, int c, int d) {
    int k = ws_start(x, c);
    return k < c && d < x->n && spot_ok(x, k, -1) && spot_ok(x, x->n, -1);
}

/* Delete candidates built from base: cut [a - t, b + u) for t over the hidden run before a and
 * u over the hidden run after b, by t + u ascending. For an emptied run (the character is the
 * only visible byte between two hidden runs) by t + u descending, so its whole pair goes.
 * Then the move-closer-back candidates for the same cuts, in the same order: the rest of the
 * hidden run moves in front of the whitespace before the cut (its pass usually comes from tier
 * B, as that whitespace loses the style). Returns 1 when one passes tier A. */
static int try_cuts(Edit *x, const char *base, Tally *tl) {
    static const int KINDS[] = { K_CUT, K_BACK };
    int np = x->a - x->p, nn = x->n - x->b, max = np + nn, emptied = np > 0 && nn > 0;
    for (int kd = 0; kd < 2; kd++)
        for (int i = 0; i <= max; i++) {
            int sum = emptied ? max - i : i;
            for (int t = 0; t <= np; t++) {
                int u = sum - t, c = x->a - t, d = x->b + u;
                if (u < 0 || u > nn || !spot_ok(x, c, x->a) || !spot_ok(x, d, x->b)) continue;
                if (KINDS[kd] == K_BACK && !back_ok(x, c, d)) continue;
                if (consider(x, (Cand){ .kind = KINDS[kd], .c = c, .d = d, .base = base }, tl)) return 1;
            }
        }
    return 0;
}

static int try_cands(Edit *x, const char *base, Tally *tl) {
    return x->op == OP_INSERT ? try_inserts(x, base, tl) : try_cuts(x, base, tl);
}

/* Tier A (exact) and tier B (relaxed) over the candidates from s, then tiers A′ and B′: the same
 * on the text with '_' emphasis markers rewritten as '*'. Returns 1 when one passes (o then
 * holds it). Sets *nus to the number of rewritten markers. */
static int exact_tiers(Edit *x, Tally *t1, Tally *t2, int *nus) {
    if (try_cands(x, x->s, t1)) return 1;
    if (t1->has_b) { accept(x, &t1->b); return 1; }
    *nus = underscore_text(x);
    if (*nus) {
        if (try_cands(x, x->us, t2)) return 1;
        if (t2->has_b) { accept(x, &t2->b); return 1; }
    }
    return 0;
}

/* ---- Typing ---- */

/* Runs the typing tiers in their fixed order and leaves the accepted text and cursor in o. */
static void insert_tiers(Edit *x) {
    Tally t1 = {0}, t2 = {0};
    int nus = 0;

    /* Tiers A and B (typed whitespace may have any style), then A′ and B′. */
    if (exact_tiers(x, &t1, &t2, &nus)) return;

    /* Tier C (visible only): the visible text is right and old characters keep their styles. */
    if (t1.has_c) { accept(x, &t1.c); return; }

    /* Tier D (AutoFormat): the plain insert, if it only adds formatting. */
    if (t1.plain_mono) { accept(x, &t1.plain); return; }

    /* Tier E (escape): a typed punctuation character goes in as "\c". */
    if (x->tlen == 1 && md_is_escapable(x->t[0])) {
        Cand e = { .kind = K_ESCAPE, .c = x->off, .base = x->s };
        if (cand_eval(x, &e) & P_MONO) { x->o->cursor = e.cursor; return; }
    }

    /* Tier F (underscore): '_' markers rewritten as '*', then the plain insert. */
    if (nus && t2.plain_mono) { accept(x, &t2.plain); return; }

    /* Tier G (last resort): the plain insert. */
    accept(x, &t1.plain);
}

void fe_insert(const char *s, int len, int in_code, int off, const char *t, int tlen, int want, FeOut *o) {
    Edit x;
    edit_open(&x, s, len, in_code, o);
    x.op = OP_INSERT;
    x.t = t;
    x.tlen = tlen < 0 ? 0 : tlen;
    x.want = want & FV_INLINE;
    o->len2 = 0;

    x.off = fv_snap(s, len, &x.old.ps, x.old.hid, off);
    /* The hidden run at the cursor, and the whitespace and hidden run left of it. */
    for (x.h = x.off; x.h < len && x.old.hid[x.h]; x.h++) {}
    x.k = ws_start(&x, x.off);
    x.j = x.k;
    if (x.k < x.off)
        while (x.j > x.old.start && x.old.hid[x.j - 1]) x.j--;

    /* Expected render: the old one with t at the cursor, styled want. */
    x.v = render_index(&x.old, x.off);
    render_insert(&x.exp, x.v, t, x.tlen, x.want);

    insert_tiers(&x);
    edit_close(&x);
}

/* ---- Deleting ---- */

/* Deletes the visible character [a, b) and leaves the accepted text and cursor in o. */
static void delete_char(Edit *x, int a, int b) {
    Tally t1 = {0}, t2 = {0};
    int nus = 0;
    x->op = OP_DELETE;
    x->a = a;
    x->b = b;
    x->p = run_start(x, a);
    x->n = run_end(x, b);
    /* Expected render: the old one without the character. */
    int va = render_index(&x->old, a);
    render_cut(&x->exp, va, render_index(&x->old, b) - va);

    /* Tiers A (exact) and B (whitespace may have any style), then A′ and B′. */
    if (exact_tiers(x, &t1, &t2, &nus)) return;

    /* Tier D (monotonic plain removal) and tier G (plain removal) both take the plain removal. */
    accept(x, &t1.plain);
}

int fe_delete_back(const char *s, int len, int in_code, int off, FeOut *o) {
    Edit x;
    int i, a, found;
    edit_open(&x, s, len, in_code, o);
    /* The last visible content byte before off, and the start of its character. */
    i = vis_before(&x, off);
    found = i >= x.old.start;
    if (found) {
        for (a = i; a > x.old.start && is_cont(s[a]) && !x.old.hid[a - 1]; a--) {}
        delete_char(&x, a, i + 1);
        o->len2 = 0;
    }
    edit_close(&x);
    return found;
}

int fe_delete_forward(const char *s, int len, int in_code, int off, FeOut *o) {
    Edit x;
    int i;
    edit_open(&x, s, len, in_code, o);
    i = vis_from(&x, off);
    if (i < len) {
        delete_char(&x, i, utf8_next(s, len, i));
        o->len2 = 0;
    }
    edit_close(&x);
    return i < len;
}

/* ---- Enter ---- */

/* Offset just after the link whose text holds off: past its closing ')' (or an autolink's '>'). */
static int link_end(const Edit *x, int off) {
    int j = off;
    while (j < x->len && (x->old.ps.style[j] & ST_LINK)) j++;
    if (j < x->len && x->s[j] == '>') return j + 1;
    if (j < x->len && x->s[j] == ']')
        while (++j < x->len)
            if (x->s[j] == ')') return j + 1;
    return off;
}

/* A split being tried: its spots, the styles that span it and the opener/closer buffers. */
typedef struct {
    int off, h;     /* candidate spots c = off..h */
    int B;          /* inline styles spanning the split */
    int ws;         /* whitespace shape: closers before the left half's trailing whitespace,
                       openers after the right half's leading whitespace */
    char *y, *cl;   /* openers and closers, len + 1 bytes each */
} Split;

/* Split candidates at each spot c = off..h with openers y[0..ny): left = s[0:c] + closers,
 * right = openers + s[c:]. The closers are y reversed byte by byte (each marker is one repeated
 * character, so they nest). In the whitespace shape the closers go before the left half's
 * trailing whitespace and the openers after the right half's leading whitespace; that
 * whitespace may lose the spanning styles. Returns 1 when both halves render as expected (o
 * then holds them). */
static int split_at(Edit *x, const Split *sp, int ny) {
    FeOut *o = x->o;
    for (int i = 0; i < ny; i++) sp->cl[i] = sp->y[ny - 1 - i];
    for (int c = sp->off; c <= sp->h; c++) {
        int lw = sp->ws ? ws_start(x, c) : c, rw = sp->ws ? ws_end(x, c) : c;
        /* left = s[0:lw] + closers + s[lw:c], right = s[c:rw] + openers + s[rw:] */
        Piece left[3] = { { x->s, lw }, { sp->cl, ny }, { x->s + lw, c - lw } };
        Piece right[3] = { { x->s + c, rw - c }, { sp->y, ny }, { x->s + rw, x->len - rw } };
        Check ckl = { .flex_at = x->exp.n - (c - lw), .flex_len = c - lw, .lose_bits = sp->B };
        Check ckr = { .flex_len = rw - c, .lose_bits = sp->B };
        if (!spot_ok(x, c, sp->off) || (sp->ws && lw == c && rw == c)) continue;
        put_pieces(&o->s, &o->cap, &o->len, left, 3);
        para_parse(&x->cur, o->s, o->len, x->in_code);
        render_of(&x->cur, &x->got);
        if (!render_match(&x->exp, &x->got, &ckl)) continue;
        put_pieces(&o->s2, &o->cap2, &o->len2, right, 3);
        para_parse(&x->cur, o->s2, o->len2, x->in_code);
        render_of(&x->cur, &x->got);
        if (render_match(&x->exp2, &x->got, &ckr)) return 1;
    }
    return 0;
}

/* Most trim options kept per run end. */
#define MAXOPT 8

/* The opener runs of the spanning styles, left to right, and the ways each may be trimmed:
 * options [0, nsuf) are its marker-only suffixes, [nsuf, nopt) its marker-only prefixes, longest
 * first. Only marker characters close properly when reversed. */
typedef struct {
    int nr, rs[NMARKS], re[NMARKS];
    int nsuf[NMARKS], nopt[NMARKS], at[NMARKS][2 * MAXOPT], len[NMARKS][2 * MAXOPT];
} Runs;

/* Finds each spanning style's opener run: the hidden run before the start of the style's run
 * (walking left from off over visible bytes with it). Runs are kept left to right without
 * duplicates. */
static void split_runs(const Edit *x, const Split *sp, Runs *r) {
    const unsigned char *st = x->old.ps.style, *hid = x->old.hid;
    int q, L;
    r->nr = 0;
    for (int m = 0; m < NMARKS; m++) {
        int bit = MARKS[m].bit, first = -1, r0;
        if (!(sp->B & bit)) continue;
        for (int i = sp->off - 1; i >= x->old.start; i--) {
            if (hid[i]) continue;
            if (!(st[i] & bit)) break;
            first = i;
        }
        if (first < 0 || (r0 = run_start(x, first)) == first) continue;
        for (q = r->nr; q > 0 && r->rs[q - 1] > r0; q--) {}
        if (q > 0 && r->rs[q - 1] == r0) continue;
        memmove(r->rs + q + 1, r->rs + q, (r->nr - q) * sizeof *r->rs);
        memmove(r->re + q + 1, r->re + q, (r->nr - q) * sizeof *r->re);
        r->rs[q] = r0;
        r->re[q] = first;
        r->nr++;
    }
    for (q = 0; q < r->nr; q++) {
        int rs = r->rs[q], re = r->re[q], n = 0;
        for (L = 0; re - L > rs && is_marker(x->s[re - L - 1]); L++) {}
        for (; L > 0 && n < MAXOPT; L--) {
            r->at[q][n] = re - L;
            r->len[q][n++] = L;
        }
        r->nsuf[q] = n;
        /* Prefixes, skipping any with the same bytes as an earlier option. */
        for (L = 0; rs + L < re && is_marker(x->s[rs + L]); L++) {}
        for (; L > 0 && n < 2 * MAXOPT; L--) {
            int dup = 0;
            for (int k = 0; k < n; k++)
                dup |= r->len[q][k] == L && !memcmp(x->s + r->at[q][k], x->s + rs, L);
            if (dup) continue;
            r->at[q][n] = rs;
            r->len[q][n++] = L;
        }
        r->nopt[q] = n;
    }
}

/* Most opener combinations tried per phase of a split. */
#define SPLIT_TRIES 32

/* Split candidates with each run trimmed to one of its options, the last run changing fastest.
 * Phase 1 uses only the suffixes; phase 2 also the prefixes, skipping what phase 1 tried. The
 * untrimmed runs are never repeated. Returns 1 when one passes. */
static int split_trimmed(Edit *x, const Split *sp, const Runs *r, int phase) {
    int pick[NMARKS] = {0}, lim[NMARKS], tries = 0, q;
    if (!r->nr) return 0;
    for (q = 0; q < r->nr; q++)
        if (!(lim[q] = phase == 1 ? r->nsuf[q] : r->nopt[q])) return 0;
    for (;;) {
        int full = 1, suf = 1, ny = 0;
        for (q = 0; q < r->nr; q++) {
            full &= r->len[q][pick[q]] == r->re[q] - r->rs[q];
            suf &= pick[q] < r->nsuf[q];
        }
        if (!full && (phase == 1 || !suf)) {
            for (q = 0; q < r->nr; q++) {
                memcpy(sp->y + ny, x->s + r->at[q][pick[q]], r->len[q][pick[q]]);
                ny += r->len[q][pick[q]];
            }
            if (split_at(x, sp, ny)) return 1;
            if (++tries == SPLIT_TRIES) return 0;
        }
        for (q = r->nr - 1; q >= 0 && pick[q] == lim[q] - 1; q--) pick[q] = 0;
        if (q < 0) return 0;
        pick[q]++;
    }
}

/* Split candidates for the styles that span the split. The openers are their opener runs; if
 * those fail, each run is trimmed to a marker-only suffix (inner markers sit at the end of an
 * opener run), then also to a marker-only prefix (a link's '[' can follow the markers). All of
 * that in the plain shape first, then in the whitespace shape. Returns 1 when one passes. */
static int split_spanning(Edit *x, Split *sp) {
    Runs r;
    split_runs(x, sp, &r);
    for (sp->ws = 0; sp->ws <= 1; sp->ws++) {
        int ny = 0;
        for (int q = 0; q < r.nr; q++) {
            memcpy(sp->y + ny, x->s + r.rs[q], r.re[q] - r.rs[q]);
            ny += r.re[q] - r.rs[q];
        }
        if (split_at(x, sp, ny) || split_trimmed(x, sp, &r, 1) || split_trimmed(x, sp, &r, 2)) return 1;
    }
    return 0;
}

void fe_split(const char *s, int len, int in_code, int off, FeOut *o) {
    Edit x;
    Split sp = {0};
    int l, r, v;
    edit_open(&x, s, len, in_code, o);
    const unsigned char *st = x.old.ps.style, *hid = x.old.hid;
    off = fv_snap(s, len, &x.old.ps, hid, off);

    /* Inside link text (a link byte on both sides) the split moves to just after the link. */
    l = vis_before(&x, off);
    r = vis_from(&x, off);
    if (l >= x.old.start && r < len && (st[l] & st[r] & ST_LINK)) {
        off = link_end(&x, off);
        r = vis_from(&x, off);
    }
    sp.off = off;
    sp.h = run_end(&x, off);
    /* The styles that span the split: on the visible bytes left and right of it. */
    sp.B = fv_style_left(s, len, &x.old.ps, hid, off) & (r < len ? st[r] : 0);

    /* Expected: the old render before the split, with the old block type (left), and after it
     * as a paragraph of its own (right). */
    v = render_index(&x.old, off);
    render_slice(&x.exp, v, x.exp.n - v, in_code ? BLK_CODE : BLK_NORMAL, &x.exp2);
    x.exp.n = v;

    sp.y = malloc(len + 1);
    sp.cl = malloc(len + 1);
    if (!(sp.B ? split_spanning(&x, &sp) : split_at(&x, &sp, 0))) {
        /* Fallback: the plain split at off. */
        Piece left = { s, off }, right = { s + off, len - off };
        put_pieces(&o->s, &o->cap, &o->len, &left, 1);
        put_pieces(&o->s2, &o->cap2, &o->len2, &right, 1);
    }
    o->cursor = 0;
    free(sp.y);
    free(sp.cl);
    edit_close(&x);
}

/* ---- Joining ---- */

/* Runs the join tiers on the joined text (x->a == x->b == the join point) and leaves the
 * accepted text and cursor in o. */
static void join_tiers(Edit *x) {
    Tally tl = {0};
    Cand c = { .kind = K_CUT, .c = x->a, .d = x->b, .base = x->s };

    /* Exact: the text renders as a's visible text followed by b's visible content. */
    if (consider(x, c, &tl)) return;

    /* Underscore: both paragraphs' '_' markers rewritten as '*', if that only adds formatting. */
    if (underscore_text(x)) {
        c.base = x->us;
        if (cand_eval(x, &c) & P_MONO) { x->o->cursor = c.cursor; return; }
    }

    /* Last resort: the plain concatenation. */
    accept(x, &tl.plain);
}

void fe_join(const char *a, int alen, const char *b, int blen, FeOut *o) {
    Edit x;
    Para pb = {0};
    int cs, n;
    memset(&x, 0, sizeof x);
    para_parse(&x.old, a, alen, 0);
    para_parse(&pb, b, blen, 0);
    cs = pb.start;
    n = alen + blen - cs;

    /* The joined text, parsed as the two paragraphs show it: a's styles and hidden bytes, then
     * those of b's content. */
    char *cat = malloc(n + 1);
    memcpy(cat, a, alen);
    memcpy(cat + alen, b + cs, blen - cs);
    cat[n] = 0;
    x.old.ps.style = realloc(x.old.ps.style, n + 1);
    x.old.ps.cap = n + 1;
    x.old.hid = realloc(x.old.hid, n + 1);
    x.old.hcap = n + 1;
    memcpy(x.old.ps.style + alen, pb.ps.style + cs, blen - cs);
    memcpy(x.old.hid + alen, pb.hid + cs, blen - cs);
    x.old.s = cat;
    x.old.len = n;
    para_free(&pb);
    x.s = cat;
    x.len = n;
    x.o = o;
    x.op = OP_DELETE;
    x.a = x.b = alen;
    edit_prepare(&x);
    o->len2 = 0;

    join_tiers(&x);
    free(cat);
    edit_close(&x);
}

/* ---- Word restyle ---- */

/* Is there a hidden marker MK in base ending at c (starting at or after lo)? */
static int mk_ends(const Edit *x, const char *base, int c, int lo) {
    int m = x->mlen;
    return c - m >= lo && memcmp(base + c - m, x->mk, m) == 0 && !memchr(x->old.hid + c - m, 0, m);
}

/* Is there a hidden marker MK in base starting at c (ending at or before hi)? */
static int mk_starts(const Edit *x, const char *base, int c, int hi) {
    int m = x->mlen;
    return c + m <= hi && memcmp(base + c, x->mk, m) == 0 && !memchr(x->old.hid + c, 0, m);
}

/* Add: MK + word + MK, the start anywhere in the hidden run before a and the end anywhere in
 * the hidden run after b, nearest the word first. */
static int restyle_add(Edit *x, Tally *tl) {
    int r = run_start(x, x->a), h = run_end(x, x->b);
    for (int c = x->a; c >= r; c--) {
        if (!spot_ok(x, c, x->a)) continue;
        for (int d = x->b; d <= h; d++)
            if (spot_ok(x, d, x->b) && consider(x, (Cand){ .kind = K_MARKS, .c = c, .d = d, .base = x->s }, tl))
                return 1;
    }
    return 0;
}

/* Remove, on base (s, or the '_'→'*' text). R1: drop an MK that ends the hidden run before a
 * and one that starts the hidden run after b. R2: close the run before the word and reopen it
 * after, with MK at the edges of the whitespace around the word (an MK already there, the run's
 * own opener or closer, is dropped instead). That whitespace may lose the bit. */
static int restyle_remove(Edit *x, int bit, const char *base, Tally *tl) {
    int a = x->a, b = x->b, aw = ws_start(x, a), bw = ws_end(x, b), r1, f;
    x->ck.flex_at = render_index(&x->old, aw);
    x->ck.flex_len = render_index(&x->old, bw) - x->ck.flex_at;
    x->ck.lose_bits = bit;

    r1 = mk_ends(x, base, a, run_start(x, a)) && mk_starts(x, base, b, run_end(x, b));
    if (r1 && consider(x, (Cand){ .kind = K_MARKS, .c = a, .d = b, .f = M_DROP_C | M_DROP_D, .base = base }, tl))
        return 1;

    f = (mk_ends(x, base, aw, x->old.start) ? M_DROP_C : 0) | (mk_starts(x, base, bw, x->len) ? M_DROP_D : 0);
    if (r1 && aw == a && bw == b && f == (M_DROP_C | M_DROP_D)) return 0;   /* the same as R1 */
    return consider(x, (Cand){ .kind = K_MARKS, .c = aw, .d = bw, .f = f, .base = base }, tl);
}

int fe_restyle(const char *s, int len, int in_code, int a, int b, int bit, FeOut *o) {
    FeOut tmp = {0};
    Edit x;
    Tally tl = {0};
    int m, va, vb, has, ok;
    for (m = 0; m < NMARKS && MARKS[m].bit != bit; m++) {}
    if (m == NMARKS || a < 0 || b > len || a >= b) return 0;

    /* Candidates go to tmp, so o is left as it was when none passes. */
    edit_open(&x, s, len, in_code, &tmp);
    x.a = a;
    x.b = b;
    x.mk = MARKS[m].mk;
    x.mlen = (int)strlen(x.mk);

    /* Expected render: the old one with the bit flipped on the word. */
    va = render_index(&x.old, a);
    vb = render_index(&x.old, b);
    has = vb > va;
    for (int i = va; i < vb; i++) has &= (x.exp.st[i] & bit) != 0;
    for (int i = va; i < vb; i++) x.exp.st[i] = (unsigned char)(has ? x.exp.st[i] & ~bit : x.exp.st[i] | bit);

    if (vb == va) ok = 0;
    else if (!has) ok = restyle_add(&x, &tl);
    /* Remove: R1 and R2, then (last tier) both again on the text with '_' markers as '*'. */
    else ok = restyle_remove(&x, bit, s, &tl) || (underscore_text(&x) && restyle_remove(&x, bit, x.us, &tl));
    edit_close(&x);
    if (!ok) {
        free(tmp.s);
        return 0;
    }
    free(o->s);
    o->s = tmp.s;
    o->cap = tmp.cap;
    o->len = tmp.len;
    o->cursor = tmp.cursor;
    o->len2 = 0;
    return 1;
}
