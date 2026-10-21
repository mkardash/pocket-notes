/* The note editor: formatted Markdown (or plain text in Zen mode), cursor, keyboards, autosave.
 * Typing redraws only the changed lines with a fast e-ink update; a clean refresh follows once
 * typing pauses. Lines can differ in height (headings), so positions are found by walking lines. */
#include "app.h"
#include "osk.h"
#include "colors.h"
#include "../core/book.h"
#include "../core/doc.h"
#include "../core/fmtedit.h"
#include "../core/fmtview.h"
#include "../core/layout.h"
#include "../core/mdstyle.h"
#include "../core/storage.h"
#include "../core/utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AUTOSAVE_MS 4000
#define CLEAN_MS 1200
#define REPEAT_FIRST_MS 450
#define REPEAT_NEXT_MS 90
#define LONG_PRESS_MS 500
#define HISTORY_KEEP 20

static char e_project[1024], e_path[1024], e_title[512];
static Doc *doc = NULL;
static Osk osk;
static int kb_visible = 1;
static int top_p = 0, top_l = 0;        /* first visible line */
static int pref_x = -1;                 /* remembered screen x for up/down moves */
static int save_failed = 0, backed_up = 0;
static int pressed_key = -1, repeated = 0, long_opened = 0;
static int down_x = -1, down_y = -1, down_in_text = 0;
static int needs_clean = 0;
static int render_ver = 1;              /* bump to re-layout everything (font, width, mode) */
static unsigned char *in_code = NULL;   /* per paragraph: inside a fenced code block */
static int in_code_cap = 0;
#ifdef HOST_SIM
static int drawn_markup = 0;            /* sim checks: ST_MARK bytes drawn as characters since the last full draw */
#endif
static int type_style = 0;              /* Formatted view: inline style (FV_INLINE bits) typed text gets */

/* ---------- geometry ---------- */

static int zen(void) { return g_settings.zen; }
static int src(void) { return !g_settings.zen && g_settings.source_view; }   /* Source view: raw text in the mono font */
static int fmt(void) { return !g_settings.zen && !g_settings.source_view; }   /* Formatted view */
static int own_kb(void) { return kb_visible; }
static int zen_bar(void) { return g_header_h * 6 / 10; }
static int status_h(void) { return zen() ? 0 : f_small->height + 24; }
static int kb_top(void) { return own_kb() ? SH - osk_height() : SH; }
static int status_y(void) { return kb_top() - status_h(); }
static Rect text_rect(void) {
    int x = g_margin * 3 / 2, y = zen() ? zen_bar() : g_header_h + 12;
    return rect_make(x, y, SW - 2 * x, status_y() - 8 - y);
}
static Rect status_rect(void) { return rect_make(0, status_y(), SW, status_h()); }
static Rect zen_exit_rect(void) { return rect_make(SW - zen_bar() - 8, 0, zen_bar(), zen_bar()); }
static Rect zen_kb_rect(void) { return rect_make(SW - 2 * zen_bar() - 16, 0, zen_bar(), zen_bar()); }
/* Header buttons from the right: keyboard, settings, formatting, Zen, quick note. */
enum { SLOT_KEYBOARD, SLOT_SETTINGS, SLOT_FORMAT, SLOT_SOURCE, SLOT_ZEN, SLOT_QUICK };

/* ---------- fenced code blocks ---------- */

/* Recomputes which paragraphs are inside ``` blocks. Returns 1 if anything changed. */
static int compute_fences(void) {
    if (in_code_cap < doc->n) {
        int old = in_code_cap;
        in_code_cap = doc->n + 256;
        in_code = realloc(in_code, in_code_cap);
        memset(in_code + old, 2, in_code_cap - old);   /* 2 = unknown, counts as a change */
    }
    int state = 0, changed = 0;
    for (int i = 0; i < doc->n; i++) {
        unsigned char v;
        if (md_is_fence(doc->p[i].s, doc->p[i].len)) { v = 0; state = !state; }
        else v = (unsigned char)state;
        if (in_code[i] != v) { in_code[i] = v; changed = 1; }
    }
    return changed;
}

/* ---------- per-paragraph layout (cached by paragraph generation) ---------- */

typedef struct {
    unsigned gen;
    int code, ver;
    ParaStyle ps;
    unsigned char *hid;     /* per byte: 1 = not drawn in Formatted view, zero width */
    int hid_cap;
    ifont *font;            /* main font of the paragraph */
    int lh, space_before;
    int first_x, rest_x, first_w, rest_w;
    int n, cap;
    LineSpan *lines;
} Lay;

static Lay *cache = NULL;
static int cache_size = 0, cache_used = 0;

static void cache_clear(void) {
    for (int i = 0; i < cache_size; i++) { free(cache[i].lines); free(cache[i].hid); md_free(&cache[i].ps); }
    free(cache);
    cache = NULL;
    cache_size = cache_used = 0;
}

/* Measuring context: which font each byte uses. */
typedef struct { const char *base; const void *lay; } MCtx;

static ifont *font_for(const void *lv, unsigned char st);

static int measure_styled(void *ctx, const char *s, int len);

static ifont *font_for_type(BlockType t, int level, unsigned char st) {
    if (g_settings.zen) return f_text;
    if (g_settings.source_view) return f_mono;
    switch (t) {
    case BLK_HEADING: return f_head[level - 1];
    case BLK_CODE: case BLK_FENCE: case BLK_TABLE: case BLK_TABLE_RULE: return f_mono;
    default: break;
    }
    if (st & ST_CODE) return f_mono;
    if ((st & ST_BOLD) && (st & ST_ITALIC)) return f_text_bi;
    if (st & ST_BOLD) return f_text_b;
    if (st & ST_ITALIC) return f_text_i;
    return f_text;
}

static ifont *font_for(const void *lv, unsigned char st) {
    const Lay *l = lv;
    return font_for_type(l->ps.type, l->ps.level, st);
}

static int measure_styled(void *ctx, const char *s, int len) {
    MCtx *m = ctx;
    const Lay *l = m->lay;
    int off = (int)(s - m->base), w = 0, i = 0;
    while (i < len) {
        if (l->hid[off + i]) { i++; continue; }   /* hidden bytes have no width */
        ifont *f = font_for(l, l->ps.style[off + i]);
        int j = i + 1;
        while (j < len && !l->hid[off + j] && font_for(l, l->ps.style[off + j]) == f) j++;
        SetFont(f, BLACK);
        w += measure_current(NULL, s + i, j - i);
        i = j;
    }
    return w;
}

static MCtx ctx_of(const Lay *l, int p) { MCtx m = { doc->p[p].s, l }; return m; }

static int width_of(const Lay *l, int p, int from, int to) {
    MCtx m = ctx_of(l, p);
    return to > from ? measure_styled(&m, doc->p[p].s + from, to - from) : 0;
}

static void compute_layout(Lay *l, int p) {
    Para *para = &doc->p[p];
    int W = text_rect().w;
    if (zen()) {
        if (l->ps.cap < para->len + 1) { l->ps.cap = para->len + 64; l->ps.style = realloc(l->ps.style, l->ps.cap); }
        memset(l->ps.style, 0, para->len + 1);
        l->ps.type = para->len ? BLK_NORMAL : BLK_EMPTY;
        l->ps.marker_len = 0;
        l->ps.bullet_off = l->ps.box_off = -1;
        l->ps.level = 0;
    } else {
        md_parse(para->s, para->len, l->code, &l->ps);
    }
    if (l->hid_cap < para->len + 1) { l->hid_cap = para->len + 64; l->hid = realloc(l->hid, l->hid_cap); }
    memset(l->hid, 0, para->len + 1);
    if (fmt()) fv_hidden(para->s, para->len, &l->ps, l->hid);
    l->font = font_for(l, 0);
    BlockType t = l->ps.type;
    l->lh = l->font->height * 135 / 100;
    l->space_before = 0;
    if (fmt() && l->ps.type == BLK_FENCE) l->lh = f_text->height / 3;
    if (fmt() && l->ps.type == BLK_TABLE_RULE) l->lh = f_text->height / 2;
    if (src()) {   /* raw text: no hanging indent, first-line indent or heading spacing */
        l->first_x = l->rest_x = 0;
        l->first_w = l->rest_w = W;
        MCtx ms = ctx_of(l, p);
        l->n = layout_wrap_first(para->s, para->len, W, W, measure_styled, &ms, &l->lines, &l->cap);
        return;
    }
    if (t == BLK_HEADING) { l->lh = l->font->height * 125 / 100; if (p > 0) l->space_before = f_text->height / 2; }
    l->first_x = l->rest_x = 0;
    l->first_w = l->rest_w = W;
    if (t == BLK_BULLET || t == BLK_NUMBER || t == BLK_TODO || t == BLK_DONE || t == BLK_QUOTE) {
        int hang = width_of(l, p, 0, l->ps.marker_len);
        l->rest_x = hang;
        l->rest_w = W - hang;
    } else if (t == BLK_NORMAL && g_settings.indent) {
        int indent = g_settings.font_size * 3 / 2;
        l->first_x = indent;
        l->first_w = W - indent;
    }
    MCtx m = ctx_of(l, p);
    l->n = layout_wrap_first(para->s, para->len, l->first_w, l->rest_w, measure_styled, &m, &l->lines, &l->cap);
}

static Lay *lay(int p) {
    Para *para = &doc->p[p];
    if (cache_size == 0 || cache_used * 2 > cache_size) {
        int want = 1024;
        while (want < doc->n * 4) want *= 2;
        cache_clear();
        cache = calloc(want, sizeof(Lay));
        cache_size = want;
    }
    unsigned h = (para->gen * 2654435761u) & (cache_size - 1);
    while (cache[h].gen && cache[h].gen != para->gen) h = (h + 1) & (cache_size - 1);
    Lay *l = &cache[h];
    if (!l->gen) { l->gen = para->gen; l->ver = 0; cache_used++; }
    int code = in_code && p < in_code_cap ? in_code[p] : 0;
    if (l->ver != render_ver || l->code != code) {
        l->code = code;
        l->ver = render_ver;
        compute_layout(l, p);
    }
    return l;
}

static void relayout_all(void) { render_ver++; }

/* ---------- visual line positions ---------- */

typedef struct { int p, l; } Pos;

static int pos_next(Pos *a) {
    if (a->l + 1 < lay(a->p)->n) { a->l++; return 1; }
    if (a->p + 1 < doc->n) { a->p++; a->l = 0; return 1; }
    return 0;
}

static int pos_prev(Pos *a) {
    if (a->l > 0) { a->l--; return 1; }
    if (a->p > 0) { a->p--; a->l = lay(a->p)->n - 1; return 1; }
    return 0;
}

static int pos_cmp(Pos a, Pos b) { return a.p != b.p ? a.p - b.p : a.l - b.l; }

static int line_h(Pos a) { Lay *l = lay(a.p); return l->lh + (a.l == 0 ? l->space_before : 0); }
static int line_x(Pos a) { Lay *l = lay(a.p); return text_rect().x + (a.l == 0 ? l->first_x : l->rest_x); }

static Pos cursor_pos(void) {
    Lay *l = lay(doc->cp);
    Pos c = { doc->cp, l->n - 1 };
    for (int i = 0; i < l->n; i++)
        if (doc->co < l->lines[i].start + l->lines[i].len || i == l->n - 1) { c.l = i; break; }
    return c;
}

/* Top y of a line if it is fully on screen, else -1. */
static int line_y(Pos a) {
    Pos t = { top_p, top_l };
    if (pos_cmp(a, t) < 0) return -1;
    Rect tr = text_rect();
    int y = tr.y, bottom = tr.y + tr.h;
    for (;;) {
        int h = line_h(t);
        if (y + h > bottom) return -1;
        if (t.p == a.p && t.l == a.l) return y;
        y += h;
        if (!pos_next(&t)) return -1;
    }
}

/* Topmost line such that the lines from it down to c fit in `height`. */
static Pos back_from(Pos c, int height) {
    Pos best = c, p = c;
    int acc = line_h(c);
    while (pos_prev(&p)) {
        acc += line_h(p);
        if (acc > height) break;
        best = p;
    }
    return best;
}

/* ---------- drawing ---------- */

static void draw_checkbox(int x, int w, int base, ifont *f, int checked, int tick) {
    int s = f->height * 60 / 100;
    if (s > w) s = w;
    int bx = x + (w - s) / 2, by = base - f->baseline + (f->height - s) / 2 + f->height / 12;
    for (int i = 0; i < 3; i++) DrawRect(bx + i, by + i, s - 2 * i, s - 2 * i, BLACK);
    if (checked) {
        for (int t = -2; t <= 2; t++) {
            DrawLine(bx + s / 5, by + s / 2 + t, bx + s * 2 / 5, by + s * 3 / 4 + t, tick);
            DrawLine(bx + s * 2 / 5, by + s * 3 / 4 + t, bx + s * 4 / 5, by + s / 4 + t, tick);
        }
    }
}

static int is_list_marker_start(const Lay *L, BlockType t, int j) {
    return (t == BLK_BULLET || t == BLK_TODO || t == BLK_DONE) && j == L->ps.bullet_off;
}

static void draw_line(Pos a, int y) {
    Rect tr = text_rect();
    Lay *L = lay(a.p);
    Para *para = &doc->p[a.p];
    int h = line_h(a);
    BlockType t = L->ps.type;
    int styled = fmt();
    FillArea(tr.x - 12, y, tr.w + 24, h, styled && (t == BLK_CODE || t == BLK_FENCE) ? COL_CODE_BG : WHITE);
    LineSpan sp = L->lines[a.l];
    int cy = y + (a.l == 0 ? L->space_before : 0);
    int lh = L->lh;
    ifont *mf = L->font;
    int base = cy + (lh - mf->height) / 2 + mf->baseline;
    int x0 = line_x(a);

    if (styled && t == BLK_HR) {
        FillArea(tr.x, cy + lh / 2 - 1, tr.w, 3, DGRAY);
        return;
    }
    if (styled && t == BLK_TABLE_RULE) {
        FillArea(tr.x, cy + lh / 2 - 1, tr.w, 2, DGRAY);
        return;
    }
    if (styled && t == BLK_FENCE) return;   /* just the tinted strip */
    if (styled && t == BLK_QUOTE) {
        /* A bar for each '>' of the marker, on every line of the paragraph. */
        for (int i = 0; i < L->ps.marker_len; i++) {
            if (para->s[i] != '>') continue;
            int bx = tr.x + width_of(L, a.p, 0, i) + width_of(L, a.p, i, i + 1) / 2;
            FillArea(bx - 2, y, 5, h, COL_QUOTE);
        }
    }

    char buf[2048];
    int x = x0, i = sp.start, end = sp.start + sp.len;
    while (i < end) {
        unsigned char st = L->ps.style[i];
        if (L->hid[i]) { i++; continue; }   /* hidden bytes take no room */
        if (styled && t == BLK_TABLE && para->s[i] == '|' && (st & ST_MARK)) {
            int pw = width_of(L, a.p, i, i + 1);
            FillArea(x + pw / 2 - 1, cy, 2, lh, DGRAY);
            x += pw;
            i++;
            continue;
        }
        if (styled && a.l == 0) {
            if ((t == BLK_TODO || t == BLK_DONE) && i == L->ps.bullet_off) {
                int box_end = L->ps.box_off + 3;
                int lead = width_of(L, a.p, i, L->ps.box_off), bw = width_of(L, a.p, L->ps.box_off, box_end);
                draw_checkbox(x + lead, bw, base, mf, t == BLK_DONE, COL_TICK);
                x += lead + bw;
                i = box_end;
                continue;
            }
            if (t == BLK_BULLET && i == L->ps.bullet_off) {
                int w = width_of(L, a.p, i, i + 1);
                SetFont(f_text_b, DGRAY);
                int bw = StringWidth("•");
                DrawString(x + (w - bw) / 2, base - f_text_b->baseline, "•");
                x += w;
                i++;
                continue;
            }
            if (t == BLK_QUOTE && para->s[i] == '>' && i < L->ps.marker_len) {
                x += width_of(L, a.p, i, i + 1);
                i++;
                continue;
            }
        }
        /* A run of bytes with the same style. */
        int j = i + 1;
        while (j < end && L->ps.style[j] == st && !L->hid[j]) {
            if (styled && t == BLK_TABLE && para->s[j] == '|' && (st & ST_MARK)) break;
            if (styled && a.l == 0 && is_list_marker_start(L, t, j)) break;
            if (styled && t == BLK_QUOTE && j < L->ps.marker_len && para->s[j] == '>') break;
            j++;
        }
        ifont *f = font_for(L, st);
        int color = BLACK;
        if (styled) {
            if (st & ST_LINK) color = COL_LINK;
            else if (t == BLK_DONE && !(st & ST_MARK)) color = DGRAY;
#ifdef HOST_SIM
            if ((st & ST_MARK) && !(t == BLK_NUMBER && j <= L->ps.marker_len)) {
                /* Prefix spaces (after a bullet or '>') are gaps, not visible markup. */
                for (int k = i; k < j; k++) if (para->s[k] != ' ') drawn_markup++;
            }
#endif
        }
        int len = j - i < (int)sizeof buf - 1 ? j - i : (int)sizeof buf - 1;
        memcpy(buf, para->s + i, len);
        buf[len] = 0;
        SetFont(f, color);
        int w = StringWidth(buf);
        if (styled && (st & ST_HL)) FillArea(x, cy + 4, w, lh - 8, COL_HIGHLIGHT);
        if (styled && (st & ST_CODE) && t != BLK_CODE) FillArea(x - 2, cy + 6, w + 4, lh - 12, COL_CODE_BG);
        SetFont(f, color);
        DrawString(x, base - f->baseline, buf);
        if (styled && ((st & ST_STRIKE) || (t == BLK_DONE && !(st & ST_MARK) && i >= L->ps.marker_len)))
            FillArea(x, base - f->height * 28 / 100, w, 2, color);
        if (styled && (st & ST_LINK)) FillArea(x, base + 4, w, 2, color);
        x += w;
        i = j;
    }

    Pos c = cursor_pos();
    if (c.p == a.p && c.l == a.l) {
        int cx = x0 + width_of(L, a.p, sp.start, doc->co);
        FillArea(cx - 1, cy + (lh - mf->height) / 2 - 2, 3, mf->height + 4, BLACK);
    }
}

/* Draws lines from `from` to `to` (inclusive), or to the bottom when to is NULL. Returns the area. */
static Rect draw_range(Pos from, const Pos *to) {
    Rect tr = text_rect();
    Pos t = { top_p, top_l };
    if (pos_cmp(from, t) < 0) from = t;
    int y = line_y(from);
    if (y < 0) return rect_make(0, 0, 0, 0);
    int y0 = y, bottom = tr.y + tr.h;
#ifdef HOST_SIM
    if (!to) drawn_markup = 0;
#endif
    Pos a = from;
    for (;;) {
        int h = line_h(a);
        if (y + h > bottom) break;
        draw_line(a, y);
        y += h;
        if (to && a.p == to->p && a.l == to->l) break;
        if (!pos_next(&a)) break;
    }
    if (!to && y < bottom) { FillArea(tr.x - 12, y, tr.w + 24, bottom - y, WHITE); y = bottom; }
    return rect_make(tr.x - 12, y0, tr.w + 24, y - y0);
}

static void draw_status(void) {
    if (zen()) return;
    Rect r = status_rect();
    FillArea(r.x, r.y, r.w, r.h, WHITE);
    FillArea(g_margin, r.y, SW - 2 * g_margin, 1, LGRAY);
    char left[128], right[160];
    snprintf(left, sizeof left, T(S_WORDS), doc ? doc_word_count(doc) : 0);
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    const char *state = save_failed ? T(S_UNSAVED) : (doc && doc->modified ? "•" : T(S_SAVED));
    snprintf(right, sizeof right, "%s   %s   %02d:%02d   %d%%", state,
             keys_layout_short((KbLayout)g_settings.layout), tmv.tm_hour, tmv.tm_min, GetBatteryPower());
    Rect inner = rect_make(g_margin, r.y, SW - 2 * g_margin, r.h);
    draw_text_fit(f_small, DGRAY, inner, left, 0);
    draw_text_fit(f_small, DGRAY, inner, right, 2);
}

static void draw(void) {
    if (zen()) {
        FillArea(0, 0, SW, zen_bar(), WHITE);
        draw_icon(ICON_KEYBOARD, zen_kb_rect(), LGRAY);
        draw_icon(ICON_ZEN_EXIT, zen_exit_rect(), LGRAY);
    } else {
        draw_header(e_title, ICON_BACK, 6);
        draw_icon(ICON_KEYBOARD, header_slot(SLOT_KEYBOARD), BLACK);
        if (own_kb()) FillArea(header_slot(SLOT_KEYBOARD).x + 24, g_header_h - 14, g_header_h - 48, 5, BLACK);
        draw_icon(ICON_SETTINGS, header_slot(SLOT_SETTINGS), BLACK);
        draw_icon(ICON_FORMAT, header_slot(SLOT_FORMAT), BLACK);
        draw_icon(ICON_SOURCE, header_slot(SLOT_SOURCE), BLACK);
        if (src()) FillArea(header_slot(SLOT_SOURCE).x + 24, g_header_h - 14, g_header_h - 48, 5, BLACK);
        draw_icon(ICON_ZEN, header_slot(SLOT_ZEN), BLACK);
        draw_icon(ICON_BULB, header_slot(SLOT_QUICK), BLACK);
    }
    if (!doc) return;
    draw_range((Pos){ top_p, top_l }, NULL);
    draw_status();
    if (own_kb()) osk_draw(&osk);
}

/* ---------- scrolling ---------- */

static void clamp_top(void) {
    if (top_p >= doc->n) { top_p = doc->n - 1; top_l = 0; }
    if (top_p < 0) top_p = 0;
    if (top_l >= lay(top_p)->n) top_l = lay(top_p)->n - 1;
    if (top_l < 0) top_l = 0;
}

/* If the end of the note is on screen with empty space below it, scrolls back to fill the space. */
static void fill_view(void) {
    Pos last = { doc->n - 1, lay(doc->n - 1)->n - 1 };
    if (line_y(last) < 0) return;
    Pos n = back_from(last, text_rect().h);
    Pos t = { top_p, top_l };
    if (pos_cmp(n, t) < 0) { top_p = n.p; top_l = n.l; }
}

/* Scrolls so the cursor is visible. Returns 1 if the view moved. */
static int ensure_cursor_visible(void) {
    Pos c = cursor_pos(), t = { top_p, top_l };
    Rect tr = text_rect();
    if (pos_cmp(c, t) < 0) {
        Pos n = back_from(c, tr.h / 4);
        top_p = n.p; top_l = n.l;
        return 1;
    }
    if (line_y(c) >= 0) return 0;
    /* Below the view: the cursor goes two thirds down, so typing runs a while before the next jump. */
    Pos n = back_from(c, tr.h * 2 / 3);
    top_p = n.p; top_l = n.l;
    return 1;
}

/* ---------- refresh scheduling ---------- */

static void clean_refresh(void);
static void autosave(void);

static void after_input(void) {
    needs_clean = 1;
    SetHardTimer("pn_clean", clean_refresh, CLEAN_MS);
}

static void clean_refresh(void) {
    if (!needs_clean || popup_active()) return;
    needs_clean = 0;
    draw_status();
    int top = zen() ? 0 : g_header_h;
    app_update(rect_make(0, top, SW, SH - top), UPD_PARTIAL);
}

static void full_text_redraw(int quality) {
    Rect r = draw_range((Pos){ top_p, top_l }, NULL);
    app_update(r, quality);
    after_input();
}

/* ---------- auto capitals for the on-screen keyboard ---------- */

static int at_sentence_start(void) {
    Para *p = &doc->p[doc->cp];
    int ml = zen() ? 0 : lay(doc->cp)->ps.marker_len;
    int i = doc->co;
    if (i <= ml) return 1;
    int spaces = 0;
    while (i > ml && p->s[i - 1] == ' ') { i--; spaces++; }
    if (i <= ml) return 1;
    if (!spaces) return 0;
    char c = p->s[i - 1];
    if (c == '.' || c == '!' || c == '?') return 1;
    return i >= 3 && memcmp(p->s + i - 3, "…", 3) == 0;
}

static void update_auto_shift(void) {
    if (!own_kb()) return;
    if (osk_auto_shift(&osk, at_sentence_start())) {
        osk_draw(&osk);
        app_update(osk.area, UPD_FAST);
    }
}

/* ---------- editing ---------- */

static void mark_edited(void) {
    SetHardTimer("pn_autosave", autosave, AUTOSAVE_MS);
}

static void normalize_cursor(void);

typedef struct { Pos before; unsigned above; int n0, lines0, lh0, type0; } EditMark;

/* Formatted view: a hash of what lines [0, l) of paragraph p show (block type, each visible
 * byte with its style, and where the lines break). An edit can restyle text before the cursor
 * line, as when a typed closer bolds a whole phrase, and those lines must then be redrawn. */
static unsigned shown_hash(int p, int l) {
    Lay *L = lay(p);
    const char *s = doc->p[p].s;
    unsigned h = 2166136261u;
#define MIX(v) (h = (h ^ (unsigned)(v)) * 16777619u)
    MIX(L->ps.type);
    MIX(L->ps.level);
    for (int k = 0; k < l && k < L->n; k++) {
        for (int i = L->lines[k].start; i < L->lines[k].start + L->lines[k].len; i++)
            if (!L->hid[i]) { MIX((unsigned char)s[i]); MIX(L->ps.style[i]); }
        MIX(0x100);         /* a line break */
    }
#undef MIX
    return h;
}

static EditMark edit_begin(void) {
    EditMark m;
    m.before = cursor_pos();
    pos_prev(&m.before);    /* wrapping can pull a word up one line */
    m.above = fmt() ? shown_hash(m.before.p, m.before.l) : 0;
    Lay *l = lay(doc->cp);
    m.n0 = doc->n;
    m.lines0 = l->n;
    m.lh0 = l->lh;
    m.type0 = (int)l->ps.type;
    return m;
}

/* Redraws after an edit: from the line above the old cursor line to the end of the cursor's
 * paragraph, or to the bottom when paragraphs or line counts/heights changed. In Formatted view
 * the redraw starts at that paragraph's first line when the lines above it show something else. */
static void edit_end(EditMark m) {
    pref_x = -1;
    mark_edited();
    int fences = compute_fences();
    int cp0 = doc->cp;
    normalize_cursor();
    int moved = doc->cp != cp0;     /* the cursor left the edited paragraph */
    clamp_top();
    if (fmt() && m.before.l > 0 && m.before.p < doc->n && shown_hash(m.before.p, m.before.l) != m.above)
        m.before.l = 0;
    if (m.before.p >= doc->n) { m.before.p = doc->n - 1; m.before.l = 0; }
    if (m.before.l >= lay(m.before.p)->n) m.before.l = lay(m.before.p)->n - 1;
    int above = pos_cmp(cursor_pos(), m.before) < 0;   /* the cursor went above the redrawn lines */
    int scrolled = ensure_cursor_visible();
    if (fences || moved || above || scrolled) { full_text_redraw(UPD_FAST); update_auto_shift(); return; }
    Lay *l = lay(doc->cp);
    int to_bottom = doc->n != m.n0 || l->n != m.lines0 || l->lh != m.lh0 || (int)l->ps.type != m.type0;
    Pos end = { doc->cp, l->n - 1 };
    Rect r = draw_range(m.before, to_bottom ? NULL : &end);
    app_update(r, UPD_FAST);
    after_input();
    update_auto_shift();
}

/* Inserts the text byte for byte; '\n' splits paragraphs. */
static void plain_insert(const char *s) {
    EditMark m = edit_begin();
    doc_insert(doc, s, (int)strlen(s));
    edit_end(m);
}

static void fmt_insert(const char *s);

static void do_insert(const char *s) {
    if (fmt()) fmt_insert(s);
    else plain_insert(s);
}

static void do_edit_action(void (*fn)(Doc *)) {
    EditMark m = edit_begin();
    fn(doc);
    edit_end(m);
}

/* Replaces bytes [from, to) of paragraph p with text. The cursor is left after the new text. */
static void replace_range(int p, int from, int to, const char *text) {
    doc_set_cursor(doc, p, to);
    while (doc->co > from) doc_backspace(doc);
    doc_insert(doc, text, (int)strlen(text));
}

static int para_hidden(int p) { return fmt() && fv_para_hidden(&lay(p)->ps); }

/* Next / previous paragraph that is not hidden in Formatted view, or -1. */
static int visible_after(int p) {
    for (p++; p < doc->n; p++) if (!para_hidden(p)) return p;
    return -1;
}

static int visible_before(int p) {
    for (p--; p >= 0; p--) if (!para_hidden(p)) return p;
    return -1;
}

/* Adds an empty paragraph at the end and puts the cursor in it. */
static void append_empty_para(void) {
    doc_insert_para(doc, doc->n, "", 0);
    compute_fences();
    doc_set_cursor(doc, doc->n - 1, 0);
    mark_edited();
}

static void reset_type_style(void);

/* Appends the empty paragraph, scrolls to it and redraws. */
static void append_and_show(void) {
    append_empty_para();
    reset_type_style();
    ensure_cursor_visible();
    full_text_redraw(UPD_FAST);
    update_auto_shift();
}

/* Formatted view: the cursor never rests in a hidden paragraph or on hidden bytes. */
static void normalize_cursor(void) {
    if (!fmt()) return;
    if (para_hidden(doc->cp)) {
        int q = visible_after(doc->cp);
        if (q >= 0) doc_set_cursor(doc, q, fv_content_start(&lay(q)->ps));
        else if ((q = visible_before(doc->cp)) >= 0) doc_set_cursor(doc, q, doc->p[q].len);
        else append_empty_para();
    }
    Lay *l = lay(doc->cp);
    Para *p = &doc->p[doc->cp];
    doc_set_cursor(doc, doc->cp, fv_snap(p->s, p->len, &l->ps, l->hid, doc->co));
}

/* Formatted view: typed text takes the style of the visible character left of the cursor.
 * Called whenever the cursor is placed other than by typing. */
static void reset_type_style(void) {
    if (!fmt()) { type_style = 0; return; }
    Lay *l = lay(doc->cp);
    Para *p = &doc->p[doc->cp];
    type_style = fv_style_left(p->s, p->len, &l->ps, l->hid, doc->co);
}

static void move_cursor_to(int p, int o) {
    Pos old = cursor_pos();
    int n0 = doc->n;
    doc_set_cursor(doc, p, o);
    normalize_cursor();
    reset_type_style();
    if (doc->n != n0) { ensure_cursor_visible(); full_text_redraw(UPD_FAST); update_auto_shift(); return; }
    if (ensure_cursor_visible()) { full_text_redraw(UPD_FAST); update_auto_shift(); return; }
    Pos c = cursor_pos();
    Rect a = rect_make(0, 0, 0, 0), b = rect_make(0, 0, 0, 0);
    if (line_y(old) >= 0) a = draw_range(old, &old);
    if (pos_cmp(c, old) != 0 && line_y(c) >= 0) b = draw_range(c, &c);
    if (a.h) app_update(a, UPD_FAST);
    if (b.h) app_update(b, UPD_FAST);
    after_input();
    update_auto_shift();
}

static int cursor_screen_x(void) {
    Pos c = cursor_pos();
    Lay *l = lay(c.p);
    return line_x(c) + width_of(l, c.p, l->lines[c.l].start, doc->co);
}

/* Byte offset in line `a` closest to screen x. */
static int offset_at(Pos a, int screen_x) {
    Lay *l = lay(a.p);
    LineSpan s = l->lines[a.l];
    int len = s.len;
    /* Don't land after the trailing space of a wrapped line: that is the next line's start. */
    if (a.l + 1 < l->n && len > 0 && doc->p[a.p].s[s.start + len - 1] == ' ') len--;
    MCtx m = ctx_of(l, a.p);
    return s.start + layout_offset_at_x(doc->p[a.p].s + s.start, len, screen_x - line_x(a), measure_styled, &m);
}

static void vertical_move(int lines) {
    if (pref_x < 0) pref_x = cursor_screen_x();
    Pos c = cursor_pos();
    int moved = 0, hidden_tail = 0;
    for (int i = 0; i < (lines < 0 ? -lines : lines); i++) {
        /* Formatted view skips the lines of hidden paragraphs. */
        Pos t = c;
        int ok;
        while ((ok = lines < 0 ? pos_prev(&t) : pos_next(&t)) && para_hidden(t.p)) {}
        if (ok) { c = t; moved++; }
        else if (lines > 0 && para_hidden(doc->n - 1)) hidden_tail = 1;
    }
    int keep = pref_x, p, o;
    if (hidden_tail && !moved) {
        append_and_show();
        pref_x = -1;
        return;
    }
    if (!moved) {
        if (lines < 0) { p = 0; o = 0; } else { p = doc->n - 1; o = doc->p[p].len; }
    } else {
        p = c.p;
        o = offset_at(c, pref_x);
    }
    move_cursor_to(p, o);
    pref_x = keep;
}

static void line_home_end(int end) {
    Pos c = cursor_pos();
    Lay *l = lay(c.p);
    LineSpan s = l->lines[c.l];
    int o = s.start;
    if (c.l == 0 && fmt() && !end) o = fv_content_start(&l->ps);  /* skip the marker */
    if (end) {
        o = s.start + s.len;
        if (c.l + 1 < l->n && o > s.start && doc->p[c.p].s[o - 1] == ' ') o--;
    }
    move_cursor_to(c.p, o);
}

/* Left (-1) / Right (+1). Formatted view steps over visible characters only, and crosses to the
 * neighbouring visible paragraph. Other views move one character. */
static void cursor_step(int dir) {
    pref_x = -1;
    if (!fmt()) {
        Doc tmp = *doc;
        if (dir < 0) doc_left(&tmp); else doc_right(&tmp);
        move_cursor_to(tmp.cp, tmp.co);
        return;
    }
    Lay *l = lay(doc->cp);
    Para *p = &doc->p[doc->cp];
    int o = dir < 0 ? fv_prev(p->s, p->len, &l->ps, l->hid, doc->co) : fv_next(p->s, p->len, &l->ps, l->hid, doc->co);
    if (o >= 0) { move_cursor_to(doc->cp, o); return; }
    int q = dir < 0 ? visible_before(doc->cp) : visible_after(doc->cp);
    if (q >= 0) move_cursor_to(q, dir < 0 ? doc->p[q].len : fv_content_start(&lay(q)->ps));
    else if (dir > 0 && doc->cp + 1 < doc->n) {     /* only hidden paragraphs follow */
        append_and_show();
    }
}

static void page_scroll(int dir) {
    Rect tr = text_rect();
    Pos top = { top_p, top_l }, n = top;
    if (dir > 0) {
        /* The last fully visible line becomes the first one. */
        Pos t = top, last = top;
        int y = 0, more = 1;
        while (y + line_h(t) <= tr.h) {
            last = t;
            y += line_h(t);
            if (!pos_next(&t)) { more = 0; break; }
        }
        if (!more) return;              /* the end of the note is already on screen */
        n = pos_cmp(last, top) == 0 ? t : last;
    } else {
        n = back_from(top, tr.h);
        if (pos_cmp(n, top) == 0 && !pos_prev(&n)) return;
    }
    top_p = n.p; top_l = n.l;
    if (line_y(cursor_pos()) < 0) { doc_set_cursor(doc, n.p, lay(n.p)->lines[n.l].start); normalize_cursor(); }
    reset_type_style();
    pref_x = -1;
    full_text_redraw(UPD_PARTIAL);
    update_auto_shift();
}

static int is_list_or_quote(BlockType t) {
    return t == BLK_BULLET || t == BLK_NUMBER || t == BLK_TODO || t == BLK_DONE || t == BLK_QUOTE;
}

/* Prefix for a new line next to a list item or quote line s: an open task, the same bullet or
 * quote marker, or the item's number plus step (1 for the line after it, 0 for one above). */
static void list_prefix(const ParaStyle *ps, const char *s, int step, char prefix[64]) {
    BlockType t = ps->type;
    if (t == BLK_NUMBER) md_prefix_for(FMT_NUMBER, ps->bullet_off, ps->number + step, prefix, 64);
    else if (t == BLK_TODO || t == BLK_DONE) md_prefix_for(FMT_TODO, ps->bullet_off, 0, prefix, 64);
    else {
        int n = ps->marker_len < 60 ? ps->marker_len : 60;
        memcpy(prefix, s, n);
        prefix[n] = 0;
    }
}

static void fmt_enter(void);

/* Enter continues lists and quotes; on an empty item it ends the list instead. */
static void do_enter(void) {
    if (zen()) { do_insert("\n"); return; }
    if (fmt()) { fmt_enter(); return; }
    Lay *l = lay(doc->cp);
    ParaStyle ps = l->ps;
    Para *p = &doc->p[doc->cp];
    BlockType t = ps.type;
    if (!is_list_or_quote(t)) {
        do_insert("\n");
        return;
    }
    if (doc->co < ps.marker_len) { do_insert("\n"); return; }
    if (p->len <= ps.marker_len) {
        EditMark m = edit_begin();
        replace_range(doc->cp, 0, p->len, "");
        edit_end(m);
        return;
    }
    char prefix[64];
    list_prefix(&ps, p->s, 1, prefix);
    char text[80];
    snprintf(text, sizeof text, "\n%s", prefix);
    do_insert(text);
}

/* Ticks or unticks a task. The cursor may be elsewhere, so the task's own lines are redrawn. */
static void toggle_checkbox(int p) {
    Lay *l = lay(p);
    if (l->ps.box_off < 0) return;
    int cp = doc->cp, co = doc->co, box = l->ps.box_off;
    const char *mark = l->ps.type == BLK_TODO ? "x" : " ";
    replace_range(p, box + 1, box + 2, mark);
    doc_set_cursor(doc, cp, co);
    mark_edited();
    compute_fences();
    Pos first = { p, 0 }, last = { p, lay(p)->n - 1 };
    Rect r = draw_range(first, &last);
    app_update(r, UPD_FAST);
    after_input();
}

/* ---------- formatting ---------- */

static int is_word_byte(unsigned char c) {
    return c >= 0x80 || (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z') || c == '\'';
}

/* The word around offset off of the cursor paragraph (a == b when there is none). */
static void word_bounds_at(int off, int *a, int *b) {
    Para *p = &doc->p[doc->cp];
    int s = off, e = off;
    while (s > 0 && is_word_byte((unsigned char)p->s[s - 1])) s--;
    while (e < p->len && is_word_byte((unsigned char)p->s[e])) e++;
    *a = s;
    *b = e;
}

static void word_bounds(int *a, int *b) { word_bounds_at(doc->co, a, b); }

static void apply_inline(const char *mk) {
    Para *p = &doc->p[doc->cp];
    int ml = (int)strlen(mk), co = doc->co, a, b;
    word_bounds(&a, &b);
    EditMark m = edit_begin();
    if (a < b) {
        if (a >= ml && b + ml <= p->len && !memcmp(p->s + a - ml, mk, ml) && !memcmp(p->s + b, mk, ml)) {
            replace_range(doc->cp, b, b + ml, "");
            replace_range(doc->cp, a - ml, a, "");
            doc_set_cursor(doc, doc->cp, co - ml);
        } else {
            replace_range(doc->cp, b, b, mk);
            replace_range(doc->cp, a, a, mk);
            doc_set_cursor(doc, doc->cp, co + ml);
        }
    } else {
        char pair[16];
        snprintf(pair, sizeof pair, "%s%s", mk, mk);
        replace_range(doc->cp, co, co, pair);
        doc_set_cursor(doc, doc->cp, co + ml);
    }
    edit_end(m);
}

static void apply_link(void) {
    int co = doc->co, a, b;
    word_bounds(&a, &b);
    EditMark m = edit_begin();
    if (a < b) {
        replace_range(doc->cp, b, b, "]()");
        replace_range(doc->cp, a, a, "[");
        doc_set_cursor(doc, doc->cp, b + 3);   /* inside the () */
    } else {
        replace_range(doc->cp, co, co, "[]()");
        doc_set_cursor(doc, doc->cp, co + 1);
    }
    edit_end(m);
}

static void apply_block(BlockFormat f) {
    Lay *l = lay(doc->cp);
    ParaStyle ps = l->ps;
    BlockType t = ps.type;
    int is_list = t == BLK_BULLET || t == BLK_NUMBER || t == BLK_TODO || t == BLK_DONE;
    int old_len = (t == BLK_NORMAL || t == BLK_EMPTY || t == BLK_HR || t == BLK_TABLE ||
                   t == BLK_TABLE_RULE || t == BLK_CODE || t == BLK_FENCE) ? 0 : ps.marker_len;
    /* Choosing the format the line already has turns it back into plain text. */
    int same = (f == FMT_H1 && t == BLK_HEADING && ps.level == 1) || (f == FMT_H2 && t == BLK_HEADING && ps.level == 2) ||
               (f == FMT_H3 && t == BLK_HEADING && ps.level == 3) || (f == FMT_BULLET && t == BLK_BULLET) ||
               (f == FMT_NUMBER && t == BLK_NUMBER) || (f == FMT_TODO && (t == BLK_TODO || t == BLK_DONE)) ||
               (f == FMT_QUOTE && t == BLK_QUOTE);
    if (same) f = FMT_NORMAL;
    int number = 1;
    if (f == FMT_NUMBER && doc->cp > 0) {
        Lay *prev = lay(doc->cp - 1);
        if (prev->ps.type == BLK_NUMBER) number = prev->ps.number + 1;
    }
    char prefix[64];
    md_prefix_for(f, is_list ? ps.bullet_off : 0, number, prefix, sizeof prefix);
    int co = doc->co, nl = (int)strlen(prefix);
    EditMark m = edit_begin();
    replace_range(doc->cp, 0, old_len, prefix);
    doc_set_cursor(doc, doc->cp, co >= old_len ? co - old_len + nl : nl);
    edit_end(m);
}

static void apply_indent(int dir) {
    Para *p = &doc->p[doc->cp];
    int co = doc->co, n = 0;
    if (dir < 0) {
        while (n < 2 && n < p->len && p->s[n] == ' ') n++;
        if (!n) return;
    }
    EditMark m = edit_begin();
    if (dir > 0) {
        replace_range(doc->cp, 0, 0, "  ");
        doc_set_cursor(doc, doc->cp, co + 2);
    } else {
        replace_range(doc->cp, 0, n, "");
        doc_set_cursor(doc, doc->cp, co > n ? co - n : 0);
    }
    edit_end(m);
}

static void insert_divider(void) {
    Para *p = &doc->p[doc->cp];
    if (p->len == 0) plain_insert("---\n");
    else { doc_set_cursor(doc, doc->cp, p->len); plain_insert("\n---\n"); }
}

static void insert_code_block(void) {
    int cp = doc->cp, co = doc->co;
    EditMark m = edit_begin();
    doc_set_cursor(doc, cp, doc->p[cp].len);
    doc_insert(doc, "\n```", 4);
    doc_set_cursor(doc, cp, 0);
    doc_insert(doc, "```\n", 4);
    doc_set_cursor(doc, cp + 1, co);
    edit_end(m);
}

/* ---------- Formatted view editing ----------
 * In Formatted view every edit goes through fmtedit, so no Markdown symbol shows and no marker
 * pair breaks. Code block lines are plain text and keep the plain edits. Lines with nothing
 * visible (dividers, fences, table rules) get the boundary rules below. Source and Zen views
 * never come here. */

static FeOut fe;                /* fmtedit results, reused by every edit */

/* Is paragraph p a line inside a fenced code block? */
static int code_at(int p) { return in_code && p < in_code_cap ? in_code[p] : 0; }

/* Paragraph insert and delete that keep the first visible line in place. */
static void ins_para(int i, const char *s, int len) {
    doc_insert_para(doc, i, s, len);
    if (i < top_p) top_p++;
}

static void del_para(int i) {
    int n0 = doc->n;
    doc_delete_para(doc, i);
    if (doc->n < n0 && i < top_p) top_p--;
}

/* Replaces paragraph i with a copy of s. doc_set_para must never read from the paragraph it
 * replaces. */
static void set_para_copy(int i, const char *s, int len) {
    char *t = malloc(len + 1);
    memcpy(t, s, len);
    doc_set_para(doc, i, t, len);
    free(t);
}

/* Closing fence for the fence line s: its indent and its run of ` or ~. Returns the length. */
static int closing_fence(const char *s, int len, char out[64]) {
    int i = md_indent(s, len), j = i;
    while (j < len && j < 63 && s[j] == s[i]) j++;
    memcpy(out, s, j);
    out[j] = 0;
    return j;
}

/* Visible content bytes of paragraph p before offset off. */
static int vis_count(int p, int off) {
    Lay *l = lay(p);
    int v = 0;
    for (int i = fv_content_start(&l->ps); i < off; i++) v += !l->hid[i];
    return v;
}

/* Canonical offset in paragraph p after v visible content bytes. */
static int vis_offset(int p, int v) {
    Lay *l = lay(p);
    Para *pp = &doc->p[p];
    int i = fv_content_start(&l->ps);
    while (i < pp->len && v > 0) v -= !l->hid[i++];
    return fv_snap(pp->s, pp->len, &l->ps, l->hid, i);
}

/* Types t[0..n) (no '\n') at the cursor in the typing style. A line that becomes a divider,
 * table rule or fence hides at once, so the cursor moves to a new empty line below it; a fence
 * also gets its closing fence. */
static void fmt_type(const char *t, int n) {
    int cp = doc->cp, newline = 0;
    EditMark m = edit_begin();
    if (code_at(cp)) {
        doc_insert(doc, t, n);
    } else {
        Para *p = &doc->p[cp];
        fe_insert(p->s, p->len, 0, doc->co, t, n, type_style, &fe);
        doc_set_para(doc, cp, fe.s, fe.len);
        doc_set_cursor(doc, cp, fe.cursor);
        BlockType bt = lay(cp)->ps.type;
        if (bt == BLK_FENCE) {
            char close[64];
            int cl = closing_fence(doc->p[cp].s, doc->p[cp].len, close);
            ins_para(cp + 1, close, cl);
        }
        if (bt == BLK_HR || bt == BLK_TABLE_RULE || bt == BLK_FENCE) {
            ins_para(cp + 1, "", 0);
            doc_set_cursor(doc, cp + 1, 0);
            newline = 1;
        }
    }
    edit_end(m);
    if (newline) reset_type_style();
}

/* Text with '\n' goes in one line at a time, with Enter between. */
static void fmt_insert(const char *s) {
    for (;;) {
        const char *nl = strchr(s, '\n');
        int n = nl ? (int)(nl - s) : (int)strlen(s);
        if (n) fmt_type(s, n);
        if (!nl) return;
        do_enter();
        s = nl + 1;
    }
}

/* Enter. On the only line of a code block, when it is empty, both fences go and an empty line
 * is left. An empty last code line leaves the block. An empty list item or quote line ends the
 * list. At the start of a text line or heading an empty line goes above, and at the start of a
 * list item or quote line an empty item of the same kind (a task unticked, a numbered item with
 * the same number); the line keeps its text and format. Otherwise the line splits, styled runs
 * are closed and reopened, and lists and quotes continue on the new line. */
static void fmt_enter(void) {
    int cp = doc->cp;
    Para *p = &doc->p[cp];
    EditMark m = edit_begin();
    if (code_at(cp)) {
        int fence_after = cp + 1 < doc->n && md_is_fence(doc->p[cp + 1].s, doc->p[cp + 1].len);
        if (p->len == 0 && fence_after && cp > 0 && md_is_fence(doc->p[cp - 1].s, doc->p[cp - 1].len)) {
            del_para(cp + 1);
            del_para(cp - 1);           /* the empty line moves up to cp - 1 */
            doc_set_cursor(doc, cp - 1, 0);
        } else if (p->len == 0 && fence_after) {
            del_para(cp);               /* the closing fence moves up to cp */
            ins_para(cp + 1, "", 0);
            doc_set_cursor(doc, cp + 1, 0);
        } else {
            doc_insert(doc, "\n", 1);
        }
        edit_end(m);
        reset_type_style();
        return;
    }
    Lay *l = lay(cp);
    BlockType t = l->ps.type;
    int cs = fv_content_start(&l->ps);
    if (is_list_or_quote(t) && p->len <= l->ps.marker_len) {
        doc_set_para(doc, cp, "", 0);
        doc_set_cursor(doc, cp, 0);
    } else if (doc->co == cs && p->len > cs) {
        char prefix[64] = "";
        if (is_list_or_quote(t)) list_prefix(&l->ps, p->s, 0, prefix);
        ins_para(cp, prefix, (int)strlen(prefix));   /* the cursor stays with the text */
    } else {
        char prefix[64] = "";
        if (is_list_or_quote(t)) list_prefix(&l->ps, p->s, 1, prefix);
        fe_split(p->s, p->len, 0, doc->co, &fe);
        int pl = (int)strlen(prefix);
        char *right = malloc(pl + fe.len2 + 1);
        memcpy(right, prefix, pl);
        memcpy(right + pl, fe.s2, fe.len2);
        doc_set_para(doc, cp, fe.s, fe.len);
        ins_para(cp + 1, right, pl + fe.len2);
        free(right);
        doc_set_cursor(doc, cp + 1, pl);
    }
    edit_end(m);
    reset_type_style();
}

/* Backspace. Inside a line it removes the visible character before the cursor. At the start of
 * a line: a heading, quote or list item becomes a plain paragraph; a divider above is deleted;
 * at the first line of a code block both fences go; after a code block or table rule the line
 * never joins it (an empty line is deleted, and the cursor goes to the last visible line above);
 * otherwise the line joins the one above. */
static void fmt_backspace(void) {
    int cp = doc->cp;
    Para *p = &doc->p[cp];
    EditMark m = edit_begin();
    if (code_at(cp)) {
        if (doc->co == 0 && cp > 0 && md_is_fence(doc->p[cp - 1].s, doc->p[cp - 1].len)) {
            int q = cp + 1;
            while (q < doc->n && !md_is_fence(doc->p[q].s, doc->p[q].len)) q++;
            if (q < doc->n) del_para(q);
            del_para(cp - 1);
        } else {
            doc_backspace(doc);
        }
    } else {
        Lay *l = lay(cp);
        int cs = fv_content_start(&l->ps);
        if (doc->co > cs && fe_delete_back(p->s, p->len, 0, doc->co, &fe)) {
            doc_set_para(doc, cp, fe.s, fe.len);
            doc_set_cursor(doc, cp, fe.cursor);
        } else if (cs > 0) {
            set_para_copy(cp, p->s + cs, p->len - cs);
            doc_set_cursor(doc, cp, 0);
        } else if (cp == 0) {
            return;
        } else {
            BlockType pt = lay(cp - 1)->ps.type;
            if (pt == BLK_HR) {
                del_para(cp - 1);
            } else if (pt == BLK_FENCE || pt == BLK_TABLE_RULE) {
                int q = visible_before(cp);
                if (q < 0) return;
                if (p->len > 0) { move_cursor_to(q, doc->p[q].len); return; }
                del_para(cp);
                doc_set_cursor(doc, q, doc->p[q].len);
            } else {
                Para *a = &doc->p[cp - 1];
                fe_join(a->s, a->len, p->s, p->len, &fe);
                doc_set_para(doc, cp - 1, fe.s, fe.len);
                del_para(cp);
                doc_set_cursor(doc, cp - 1, fe.cursor);
            }
        }
    }
    edit_end(m);
    reset_type_style();
}

/* Delete. Inside a line it removes the visible character after the cursor. At the end of a
 * line: a divider below is deleted; next to a fence or table rule nothing is deleted and the
 * cursor moves to the next visible line; otherwise the next line joins this one without its
 * block prefix. */
static void fmt_delete(void) {
    int cp = doc->cp, nx = cp + 1 < doc->n ? cp + 1 : -1;
    Para *p = &doc->p[cp];
    EditMark m = edit_begin();
    if (code_at(cp)) {
        if (doc->co < p->len || nx < 0 || !md_is_fence(doc->p[nx].s, doc->p[nx].len)) {
            doc_delete_forward(doc);
            edit_end(m);
            reset_type_style();
            return;
        }
    } else {
        Lay *l = lay(cp);
        int v = doc->co;
        while (v < p->len && l->hid[v]) v++;
        if (v < p->len && fe_delete_forward(p->s, p->len, 0, doc->co, &fe)) {
            doc_set_para(doc, cp, fe.s, fe.len);
            doc_set_cursor(doc, cp, fe.cursor);
            edit_end(m);
            reset_type_style();
            return;
        }
    }
    if (nx < 0) return;
    BlockType nt = lay(nx)->ps.type;
    if (nt == BLK_FENCE || nt == BLK_TABLE_RULE) {
        int q = visible_after(cp);
        if (q >= 0) move_cursor_to(q, fv_content_start(&lay(q)->ps));
        return;
    }
    if (nt == BLK_HR) {
        del_para(nx);
    } else {
        Para *b = &doc->p[nx];
        fe_join(p->s, p->len, b->s, b->len, &fe);
        doc_set_para(doc, cp, fe.s, fe.len);
        del_para(nx);
        doc_set_cursor(doc, cp, fe.cursor);
    }
    edit_end(m);
    reset_type_style();
}

/* B menu inline style. On a word (visible word characters on both sides of the cursor) the
 * whole visible word gets or loses the style, and the cursor stays on the same character; when
 * that can't be done safely the text is left as it is. Elsewhere the style is turned on or off
 * for what is typed next. */
static void fmt_inline(int bit) {
    int cp = doc->cp, co = doc->co;
    Para *p = &doc->p[cp];
    Lay *l = lay(cp);
    int cs = fv_content_start(&l->ps), a = co - 1, b = co;
    while (a >= cs && l->hid[a]) a--;
    while (b < p->len && l->hid[b]) b++;
    if (a < cs || b >= p->len || !is_word_byte((unsigned char)p->s[a]) || !is_word_byte((unsigned char)p->s[b])) {
        type_style ^= bit;
        return;
    }
    /* Widen to the whole visible word; hidden bytes inside it don't end it. */
    for (int i = a - 1; i >= cs; i--) {
        if (l->hid[i]) continue;
        if (!is_word_byte((unsigned char)p->s[i])) break;
        a = i;
    }
    for (int i = b + 1; i < p->len; i++) {
        if (l->hid[i]) continue;
        if (!is_word_byte((unsigned char)p->s[i])) break;
        b = i;
    }
    if (!fe_restyle(p->s, p->len, code_at(cp), a, b + 1, bit, &fe)) return;
    int v = vis_count(cp, co);
    EditMark m = edit_begin();
    doc_set_para(doc, cp, fe.s, fe.len);
    doc_set_cursor(doc, cp, vis_offset(cp, v));
    edit_end(m);
    reset_type_style();
}

/* Visible content bytes of s in Formatted view, written to out (len bytes or more). */
static int visible_text(const char *s, int len, int code, char *out) {
    static ParaStyle ps;
    static unsigned char *hid;
    static int cap;
    md_parse(s, len, code, &ps);
    if (cap < len + 1) { cap = len + 64; hid = realloc(hid, cap); }
    fv_hidden(s, len, &ps, hid);
    int n = 0;
    for (int i = fv_content_start(&ps); i < len; i++) if (!hid[i]) out[n++] = s[i];
    return n;
}

/* Writes p's text with [text](url) put in at offset at in place of cut bytes. Returns the length. */
static int with_link(char *out, const Para *p, int at, int cut, const char *text, int tl, const char *url, int ul) {
    int n = at, rest = p->len - at - cut;
    memcpy(out, p->s, at);
    out[n++] = '[';
    memcpy(out + n, text, tl);
    n += tl;
    out[n++] = ']';
    out[n++] = '(';
    memcpy(out + n, url, ul);
    n += ul;
    out[n++] = ')';
    memcpy(out + n, p->s + at + cut, rest);
    return n + rest;
}

/* B menu link, once the address is typed (NULL when cancelled). On a word (its edges count) the
 * word becomes the link text; elsewhere the address goes in as its own link text. The result
 * must show exactly the expected characters, so nothing happens where a link can't go (inside
 * another link, in a code line). */
static void link_entered(const char *url, void *ctx) {
    if (!doc || !url || !url[0]) return;
    int cp = doc->cp, co = doc->co, code = code_at(cp), ul = (int)strlen(url), a, b, h = co, ok = 0, n = 0, end = 0;
    Para *p = &doc->p[cp];
    Lay *l = lay(cp);
    while (h < p->len && l->hid[h]) h++;            /* the hidden bytes at the cursor */
    word_bounds_at(co, &a, &b);
    if (a == b) word_bounds_at(h, &a, &b);
    int v = vis_count(cp, co), cap = p->len + 2 * ul + 8;
    char *cand = malloc(cap), *want = malloc(cap), *got = malloc(cap);
    int wn = visible_text(p->s, p->len, code, want);
    if (a < b) {
        n = with_link(cand, p, a, b - a, p->s + a, b - a, url, ul);
        end = n - (p->len - b);
        ok = visible_text(cand, n, code, got) == wn && !memcmp(got, want, wn);
    } else {
        memmove(want + v + ul, want + v, wn - v);
        memcpy(want + v, url, ul);
        wn += ul;
        for (int c = co; c <= h && !ok; c++) {
            if (c < p->len && ((unsigned char)p->s[c] & 0xC0) == 0x80) continue;
            n = with_link(cand, p, c, 0, url, ul, url, ul);
            end = c + 2 * ul + 4;
            ok = visible_text(cand, n, code, got) == wn && !memcmp(got, want, wn);
        }
    }
    if (ok) {
        EditMark m = edit_begin();
        doc_set_para(doc, cp, cand, n);
        doc_set_cursor(doc, cp, end);
        edit_end(m);
        reset_type_style();
    }
    free(cand);
    free(want);
    free(got);
}

static void inline_style(const char *mk, int bit) {
    if (fmt()) fmt_inline(bit);
    else apply_inline(mk);
}

static void link_chosen(void) {
    if (fmt()) popup_prompt(T(S_LINK_URL), "", link_entered, NULL);
    else apply_link();
}

enum {
    F_NORMAL, F_H1, F_H2, F_H3, F_BULLET, F_NUMBER, F_TODO, F_QUOTE, F_CODEBLOCK,
    F_BOLD, F_ITALIC, F_STRIKE, F_HIGHLIGHT, F_CODE, F_LINK, F_DIVIDER, F_INDENT, F_OUTDENT, F_COUNT
};

static void format_chosen(int idx, void *ctx) {
    switch (idx) {
    case F_NORMAL: apply_block(FMT_NORMAL); break;
    case F_H1: apply_block(FMT_H1); break;
    case F_H2: apply_block(FMT_H2); break;
    case F_H3: apply_block(FMT_H3); break;
    case F_BULLET: apply_block(FMT_BULLET); break;
    case F_NUMBER: apply_block(FMT_NUMBER); break;
    case F_TODO: apply_block(FMT_TODO); break;
    case F_QUOTE: apply_block(FMT_QUOTE); break;
    case F_CODEBLOCK: insert_code_block(); reset_type_style(); break;
    case F_BOLD: inline_style("**", ST_BOLD); break;
    case F_ITALIC: inline_style("*", ST_ITALIC); break;
    case F_STRIKE: inline_style("~~", ST_STRIKE); break;
    case F_HIGHLIGHT: inline_style("==", ST_HL); break;
    case F_CODE: inline_style("`", ST_CODE); break;
    case F_LINK: link_chosen(); break;
    case F_DIVIDER: insert_divider(); reset_type_style(); break;
    case F_INDENT: apply_indent(1); break;
    case F_OUTDENT: apply_indent(-1); break;
    }
}

static void open_format_menu(void) {
    const char *items[F_COUNT] = {
        T(S_FMT_NORMAL), T(S_FMT_H1), T(S_FMT_H2), T(S_FMT_H3), T(S_FMT_BULLET), T(S_FMT_NUMBER),
        T(S_FMT_TODO), T(S_FMT_QUOTE), T(S_FMT_CODEBLOCK), T(S_FMT_BOLD), T(S_FMT_ITALIC),
        T(S_FMT_STRIKE), T(S_FMT_HIGHLIGHT), T(S_FMT_CODE), T(S_FMT_LINK), T(S_FMT_DIVIDER),
        T(S_FMT_INDENT), T(S_FMT_OUTDENT),
    };
    popup_menu(T(S_FORMAT), items, F_COUNT, format_chosen, NULL);
}

/* ---------- saving ---------- */

static int save_now(void) {
    if (!doc || !doc->modified) return 0;
    if (!backed_up) {
        storage_backup(e_project, e_path, time(NULL), HISTORY_KEEP);
        backed_up = 1;
    }
    int len;
    char *text = doc_text(doc, &len);
    int rc = storage_write_atomic(e_path, text, len);
    free(text);
    if (rc == 0) {
        doc->modified = 0;
        save_failed = 0;
    } else if (!save_failed) {
        save_failed = 1;
        popup_message(T(S_SAVE_FAILED));
    }
    return rc;
}

static void autosave(void) {
    if (popup_active()) { SetHardTimer("pn_autosave", autosave, AUTOSAVE_MS); return; }
    save_now();
    if (!zen()) { draw_status(); app_update(status_rect(), UPD_FAST); }
    app_flush();
}

/* ---------- screen callbacks ---------- */

static void apply_view_change(void) {
    osk_set_area(&osk, rect_make(0, SH - osk_height(), SW, osk_height()));
    relayout_all();
    normalize_cursor();
    clamp_top();
    ensure_cursor_visible();
    fill_view();
    app_save_settings();
    app_redraw(UPD_PARTIAL);
}

static void toggle_keyboard(void) {
    kb_visible = !kb_visible;
    g_settings.keyboard_visible = kb_visible;
    apply_view_change();
}

static void toggle_source(void) {
    g_settings.source_view = !g_settings.source_view;
    apply_view_change();
    reset_type_style();
    app_redraw(UPD_FULL);
}

static void toggle_zen(void) {
    g_settings.zen = !g_settings.zen;
    apply_view_change();
    reset_type_style();
    app_redraw(UPD_FULL);
}

void editor_open(const char *project_dir, const char *doc_path, int para, int off, int show_keyboard) {
    int len = 0;
    char *text = storage_read(doc_path, &len);
    if (!text) { popup_message(T(S_OPEN_FAILED)); return; }
    doc_free(doc);
    doc = doc_from_text(text, len);
    free(text);
    cache_clear();
    free(in_code);
    in_code = NULL;
    in_code_cap = 0;
    compute_fences();
    snprintf(e_project, sizeof e_project, "%s", project_dir);
    snprintf(e_path, sizeof e_path, "%s", doc_path);
    const char *base = strrchr(doc_path, '/');
    int order;
    book_parse_name(base ? base + 1 : doc_path, 0, &order, e_title, sizeof e_title);
    if (para < 0) { para = doc->n - 1; off = doc->p[para].len; }
    doc_set_cursor(doc, para, off);
    relayout_all();
    normalize_cursor();
    reset_type_style();
    backed_up = 0;
    save_failed = 0;
    pref_x = -1;
    top_p = doc->cp; top_l = 0;
    kb_visible = show_keyboard;
    osk_init(&osk, rect_make(0, SH - osk_height(), SW, osk_height()), g_settings.layout);
    snprintf(g_settings.last_book, sizeof g_settings.last_book, "%s", project_dir);
    snprintf(g_settings.last_doc, sizeof g_settings.last_doc, "%s", doc_path);
    app_save_settings();
    app_show(&screen_editor);
}

static void enter(void) {
    osk_close_variants(&osk);
    relayout_all();
    normalize_cursor();
    reset_type_style();
    clamp_top();
    /* Open with the cursor a third of the way down. */
    Pos n = back_from(cursor_pos(), text_rect().h / 3);
    top_p = n.p; top_l = n.l;
    fill_view();
    if (own_kb()) osk_auto_shift(&osk, at_sentence_start());
}

static void leave(void) {
    ClearTimer(clean_refresh);
    ClearTimer(autosave);
    save_now();
    if (doc && !strcmp(g_settings.last_doc, e_path)) {
        g_settings.last_para = doc->cp;
        g_settings.last_off = doc->co;
    }
    app_save_settings();
}

static void go_back(void) {
    save_now();
    book_screen_open(e_project);
}

static void quick_note(void) {
    save_now();
    Node *root = book_load(e_project);
    char path[1024];
    int rc = root ? book_new_idea(root, T(S_IDEAS), time(NULL), path, sizeof path) : -1;
    book_free(root);
    if (rc != 0) { popup_message(T(S_OP_FAILED)); return; }
    editor_open(e_project, path, 0, 0, 1);
}

static void change_font_size(int delta) {
    int s = g_settings.font_size + delta;
    if (s < 24 || s > 80) return;
    g_settings.font_size = s;
    app_reopen_text_font();
    apply_view_change();
}

/* Steps through the installed regular (non-bold, non-italic) fonts. */
static void cycle_font(void) {
    char **fonts = EnumFonts();
    const char *found[64];
    int n = 0;
    for (int j = 0; fonts && fonts[j] && n < 64; j++) {
        const char *f = fonts[j];
        if (strstr(f, "Bold") || strstr(f, "Italic") || strstr(f, "Oblique") || strstr(f, "Mono") ||
            strstr(f, "Symbol") || strstr(f, "Emoji")) continue;
        found[n++] = f;
    }
    if (n == 0) return;
    int cur = -1;
    for (int i = 0; i < n; i++) if (f_text->name && !strcmp(found[i], f_text->name)) cur = i;
    snprintf(g_settings.font_name, sizeof g_settings.font_name, "%s", found[(cur + 1) % n]);
    change_font_size(0);
}

enum { SET_LARGER, SET_SMALLER, SET_FONT, SET_INDENT, SET_LANGUAGE, SET_KB_LANGS, SET_REFRESH };

static void action(Action a);
static void open_kb_languages_menu(void);

/* Tapping a language toggles it and reopens the menu. The last one on stays on. */
static void kb_language_chosen(int idx, void *ctx) {
    (void)ctx;
    if (idx < 0 || idx >= LAYOUT_COUNT) return;
    unsigned bit = 1u << idx;
    if (!(g_settings.layouts & bit)) g_settings.layouts |= bit;
    else if (g_settings.layouts != bit) {
        g_settings.layouts &= ~bit;
        if (g_settings.layout == idx) {
            g_settings.layout = keys_next_layout((KbLayout)idx, g_settings.layouts);
            action(ACT_TOGGLE_LAYOUT);          /* page, status bar, save */
        }
    }
    app_save_settings();
    open_kb_languages_menu();
}

static void open_kb_languages_menu(void) {
    char text[LAYOUT_COUNT][160];
    const char *items[LAYOUT_COUNT];
    for (int i = 0; i < LAYOUT_COUNT; i++) {
        snprintf(text[i], sizeof text[i], T(g_settings.layouts & (1u << i) ? S_KB_LANG_ON : S_KB_LANG_OFF),
                 keys_layout_name((KbLayout)i));
        items[i] = text[i];
    }
    popup_menu(T(S_KB_LANGUAGES), items, LAYOUT_COUNT, kb_language_chosen, NULL);
}

static void settings_chosen(int idx, void *ctx) {
    switch (idx) {
    case SET_LARGER: change_font_size(4); break;
    case SET_SMALLER: change_font_size(-4); break;
    case SET_FONT: cycle_font(); break;
    case SET_INDENT: g_settings.indent = !g_settings.indent; apply_view_change(); break;
    case SET_LANGUAGE:
        g_settings.ui_lang = !g_settings.ui_lang;
        i18n_set(g_settings.ui_lang);
        apply_view_change();
        break;
    case SET_KB_LANGS: open_kb_languages_menu(); break;
    case SET_REFRESH: app_redraw(UPD_FULL); break;
    }
}

static void open_settings_menu(void) {
    char font_item[200];
    snprintf(font_item, sizeof font_item, T(S_FONT), f_text && f_text->name ? f_text->name : "?");
    const char *items[] = {
        T(S_LARGER), T(S_SMALLER), font_item,
        T(g_settings.indent ? S_INDENT_ON : S_INDENT_OFF),
        T(S_UI_LANGUAGE), T(S_KB_LANGUAGES), T(S_REFRESH),
    };
    popup_menu(T(S_SETTINGS), items, 7, settings_chosen, NULL);
}

static void run_osk_key(int k);

static void key_repeat(void) {
    if (pressed_key < 0) return;
    repeated = 1;
    run_osk_key(pressed_key);
    app_flush();
    SetHardTimer("pn_repeat", key_repeat, REPEAT_NEXT_MS);
}

static void long_press_fire(void) {
    if (pressed_key < 0 || !own_kb()) return;
    if (osk_open_variants(&osk, pressed_key)) {
        long_opened = 1;
        osk_draw(&osk);
        app_update(osk.area, UPD_FAST);
        app_flush();
    }
}

static void close_variants(void) {
    if (!osk.var_n) return;
    osk_close_variants(&osk);
    if (own_kb()) { osk_draw(&osk); app_update(osk.area, UPD_FAST); }
}

static void run_osk_key(int k) {
    OskResult r = osk_activate(&osk, k, app_now_ms());
    if (r.redraw_keyboard) {
        osk_draw(&osk);
        if (pressed_key >= 0) osk_draw_key(&osk, pressed_key, 1);
        app_update(osk.area, UPD_FAST);
    }
    if (r.action == ACT_TOGGLE_LAYOUT) {
        g_settings.layout = osk.letters_page;
        app_save_settings();
        if (!zen()) { draw_status(); app_update(status_rect(), UPD_FAST); }
    } else if (r.action != ACT_NONE) {
        action(r.action);
    }
    if (r.text) do_insert(r.text);
}

static void tap_text(int x, int y) {
    Rect tr = text_rect();
    Pos a = { top_p, top_l }, last = a;
    int ly = tr.y, found = 0;
    for (;;) {
        int h = line_h(a);
        if (ly + h > tr.y + tr.h) break;
        last = a;
        if (y < ly + h) { found = 1; break; }
        ly += h;
        if (!pos_next(&a)) break;
    }
    if (!found) a = last;
    if (!found && fmt() && para_hidden(doc->n - 1)) {
        Pos e = a;
        if (!pos_next(&e)) {        /* below the text, with only hidden paragraphs after it */
            pref_x = -1;
            append_and_show();
            return;
        }
    }
    Lay *l = lay(a.p);
    /* Tapping a checkbox ticks it. */
    if (fmt() && a.l == 0 && (l->ps.type == BLK_TODO || l->ps.type == BLK_DONE)) {
        int bx0 = line_x(a) + width_of(l, a.p, 0, l->ps.box_off);
        int bx1 = line_x(a) + width_of(l, a.p, 0, l->ps.box_off + 3);
        if (x >= bx0 - 20 && x <= bx1 + 20) { toggle_checkbox(a.p); return; }
    }
    pref_x = -1;
    move_cursor_to(a.p, offset_at(a, x));
}

static void pointer(int type, int x, int y) {
    if (!doc) return;
    if (osk.var_n && !own_kb()) osk_close_variants(&osk);
    if (osk.var_n) {
        /* Variants popup open: a tap on a variant types it, any other tap closes the popup. */
        if (type == EVT_POINTERUP && pressed_key >= 0 && long_opened) {
            /* Release of the press that opened the popup. */
            pressed_key = -1;
            long_opened = 0;
            osk_draw(&osk);
            app_update(osk.area, UPD_FAST);
            return;
        }
        if (type == EVT_POINTERUP) {
            int v = osk_variant_at(&osk, x, y);
            if (v >= 0) {
                OskResult r = osk_pick_variant(&osk, v);
                osk_draw(&osk);
                app_update(osk.area, UPD_FAST);
                if (r.text) do_insert(r.text);
                after_input();
            } else {
                close_variants();
            }
        }
        return;
    }
    if (own_kb() && y >= kb_top()) {
        int k = osk_key_at(&osk, x, y);
        if (type == EVT_POINTERDOWN && k >= 0) {
            pressed_key = k;
            repeated = 0;
            long_opened = 0;
            if (osk_key_has_variants(&osk, k)) SetHardTimer("pn_long", long_press_fire, LONG_PRESS_MS);
            osk_draw_key(&osk, k, 1);
            app_update(osk.rects[k], UPD_FAST);
            if (osk_key_repeats(&osk, k)) SetHardTimer("pn_repeat", key_repeat, REPEAT_FIRST_MS);
        } else if (type == EVT_POINTERUP && pressed_key >= 0) {
            int pk = pressed_key;
            pressed_key = -1;
            ClearTimer(key_repeat);
            ClearTimer(long_press_fire);
            osk_draw_key(&osk, pk, 0);
            app_update(osk.rects[pk], UPD_FAST);
            if (pk == k && !repeated) run_osk_key(k);
            after_input();
        }
        return;
    }
    if (type == EVT_POINTERUP && pressed_key >= 0) {
        /* Finger slid off the keyboard: cancel the key press. */
        int pk = pressed_key;
        pressed_key = -1;
        ClearTimer(key_repeat);
        ClearTimer(long_press_fire);
        osk_draw_key(&osk, pk, 0);
        app_update(osk.rects[pk], UPD_FAST);
        return;
    }
    if (type == EVT_POINTERDOWN) {
        down_x = x; down_y = y;
        down_in_text = rect_hit(text_rect(), x, y);
        return;
    }
    if (type != EVT_POINTERUP || down_y < 0) return;
    int dy = y - down_y, in_text = down_in_text;
    down_y = -1;
    if (in_text && dy < -SH / 12) { page_scroll(1); return; }
    if (in_text && dy > SH / 12) { page_scroll(-1); return; }
    if (zen()) {
        if (rect_hit(zen_exit_rect(), x, y)) { toggle_zen(); return; }
        if (rect_hit(zen_kb_rect(), x, y)) { toggle_keyboard(); return; }
    } else {
        if (rect_hit(header_back_rect(), x, y)) { go_back(); return; }
        if (rect_hit(header_slot(SLOT_KEYBOARD), x, y)) { toggle_keyboard(); return; }
        if (rect_hit(header_slot(SLOT_SETTINGS), x, y)) { open_settings_menu(); return; }
        if (rect_hit(header_slot(SLOT_FORMAT), x, y)) { open_format_menu(); return; }
        if (rect_hit(header_slot(SLOT_SOURCE), x, y)) { toggle_source(); return; }
        if (rect_hit(header_slot(SLOT_ZEN), x, y)) { toggle_zen(); return; }
        if (rect_hit(header_slot(SLOT_QUICK), x, y)) { quick_note(); return; }
    }
    if (in_text && rect_hit(text_rect(), x, y)) tap_text(x, y);
}

static void text_input(const char *s) {
    if (!doc) return;
    if (!strcmp(s, "\n")) do_enter();
    else do_insert(s);
}

static void action(Action a) {
    if (!doc) return;
    switch (a) {
    case ACT_BACKSPACE: if (fmt()) fmt_backspace(); else do_edit_action(doc_backspace); break;
    case ACT_DELETE: if (fmt()) fmt_delete(); else do_edit_action(doc_delete_forward); break;
    case ACT_ENTER: do_enter(); break;
    case ACT_LEFT: cursor_step(-1); break;
    case ACT_RIGHT: cursor_step(1); break;
    case ACT_UP: vertical_move(-1); break;
    case ACT_DOWN: vertical_move(1); break;
    case ACT_HOME: line_home_end(0); break;
    case ACT_END: line_home_end(1); break;
    case ACT_PAGE_UP: page_scroll(-1); break;
    case ACT_PAGE_DOWN: page_scroll(1); break;
    case ACT_SAVE: save_now(); if (!zen()) { draw_status(); app_update(status_rect(), UPD_FAST); } break;
    case ACT_BACK: if (zen()) toggle_zen(); else go_back(); break;
    case ACT_HIDE_KEYBOARD: if (kb_visible) toggle_keyboard(); break;
    case ACT_TOGGLE_LAYOUT:
        osk_set_layout(&osk, g_settings.layout);
        app_save_settings();
        if (own_kb()) { osk_draw(&osk); app_update(osk.area, UPD_FAST); }
        if (!zen()) { draw_status(); app_update(status_rect(), UPD_FAST); }
        break;
    default: break;
    }
}

static void device_key(int key) {
    switch (key) {
    case IV_KEY_NEXT: case IV_KEY_NEXT2: page_scroll(1); break;
    case IV_KEY_PREV: case IV_KEY_PREV2: page_scroll(-1); break;
    case IV_KEY_BACK: action(ACT_BACK); break;
    case IV_KEY_LEFT: action(ACT_LEFT); break;
    case IV_KEY_RIGHT: action(ACT_RIGHT); break;
    case IV_KEY_UP: action(ACT_UP); break;
    case IV_KEY_DOWN: action(ACT_DOWN); break;
    case IV_KEY_DELETE: action(ACT_BACKSPACE); break;
    case IV_KEY_OK: action(ACT_ENTER); break;
    }
}

Screen screen_editor = { enter, draw, pointer, device_key, text_input, action, leave };

#ifdef HOST_SIM
Osk *editor_sim_osk(void) { return &osk; }
int editor_sim_layout(void) { return g_settings.layout; }
Doc *editor_sim_doc(void) { return doc; }
/* In Formatted view the cursor must be on a visible paragraph at a canonical offset. */
int editor_sim_cursor_visible(void) { return line_y(cursor_pos()) >= 0; }
int editor_sim_cursor_ok(void) {
    if (!fmt()) return 1;
    Lay *l = lay(doc->cp);
    return !fv_para_hidden(&l->ps) && fv_snap(doc->p[doc->cp].s, doc->p[doc->cp].len, &l->ps, l->hid, doc->co) == doc->co;
}
int editor_sim_drawn_markup(void) { return drawn_markup; }
/* Line height of the cursor's paragraph. */
int editor_sim_line_height(void) { return lay(doc->cp)->lh; }
/* The area the note's lines are drawn in. */
void editor_sim_text_area(int *x, int *y, int *w, int *h) {
    Rect tr = text_rect();
    *x = tr.x - 12;
    *y = tr.y;
    *w = tr.w + 24;
    *h = tr.h;
}
/* Screen point of a task's checkbox (paragraph p). Returns 0 if not visible. */
int editor_sim_checkbox_point(int p, int *x, int *y) {
    Pos a = { p, 0 };
    Lay *l = lay(p);
    int ly = line_y(a);
    if (ly < 0 || l->ps.box_off < 0) return 0;
    *x = line_x(a) + width_of(l, p, 0, l->ps.box_off) + width_of(l, p, l->ps.box_off, l->ps.box_off + 3) / 2;
    *y = ly + line_h(a) / 2;
    return 1;
}
#endif
