#include "osk.h"
#include <string.h>

#define KEY_GAP 6
#define SPECIAL_BG 0xDDDDDD

int osk_height(void) { return SH * 33 / 100; }

static void layout_keys(Osk *k) {
    const OskLayout *l = keys_osk_page(k->page);
    int pad = 8;
    int row_h = (k->area.h - 2 * pad) / l->nrows;
    int usable_w = k->area.w - 2 * pad;
    k->nkeys = 0;
    k->var_n = 0;
    for (int r = 0; r < l->nrows; r++) {
        int x = k->area.x + pad, acc = 0;
        for (int i = 0; i < l->rows[r].n && k->nkeys < OSK_MAX_KEYS; i++) {
            const OskKey *def = &l->rows[r].keys[i];
            int x0 = k->area.x + pad + usable_w * acc / 120;
            acc += def->weight;
            int x1 = k->area.x + pad + usable_w * acc / 120;
            k->rects[k->nkeys] = rect_make(x0, k->area.y + pad + r * row_h, x1 - x0, row_h);
            k->defs[k->nkeys] = def;
            k->nkeys++;
            x = x1;
        }
        (void)x;
    }
}

void osk_init(Osk *k, Rect area, int layout) {
    memset(k, 0, sizeof *k);
    k->area = area;
    k->letters_page = layout >= 0 && layout < LAYOUT_COUNT ? layout : LAYOUT_EN;
    k->page = k->letters_page;
    layout_keys(k);
}

void osk_set_area(Osk *k, Rect area) {
    k->area = area;
    layout_keys(k);
}

static int is_special(const OskKey *d) { return d->type != OSK_CHAR && d->type != OSK_SPACE; }

static const char *char_label(Osk *k, const OskKey *d) {
    if (k->shift && d->upper) return d->upper;
    return d->lower;
}

void osk_draw_key(Osk *k, int i, int pressed) {
    const OskKey *d = k->defs[i];
    Rect r = k->rects[i];
    Rect in = rect_make(r.x + KEY_GAP / 2, r.y + KEY_GAP / 2, r.w - KEY_GAP, r.h - KEY_GAP);
    FillArea(r.x, r.y, r.w, r.h, WHITE);
    int active_shift = d->type == OSK_SHIFT && k->shift;
    int bg = pressed ? BLACK : (is_special(d) ? SPECIAL_BG : WHITE);
    if (active_shift && !pressed) bg = LGRAY;
    int fg = pressed ? WHITE : BLACK;
    FillArea(in.x, in.y, in.w, in.h, bg);
    DrawRect(in.x, in.y, in.w, in.h, DGRAY);
    DrawRect(in.x + 1, in.y + 1, in.w - 2, in.h - 2, DGRAY);

    switch (d->type) {
    case OSK_CHAR: draw_text_fit(f_key, fg, in, char_label(k, d), 1); break;
    case OSK_SPACE:
        draw_text_fit(f_small, pressed ? WHITE : DGRAY, in, keys_layout_name((KbLayout)k->letters_page), 1);
        break;
    case OSK_LANG: draw_text_fit(f_ui_bold, fg, in, keys_layout_short((KbLayout)k->letters_page), 1); break;
    case OSK_SYMBOLS: draw_text_fit(f_ui_bold, fg, in, "123", 1); break;
    case OSK_LETTERS: draw_text_fit(f_ui_bold, fg, in, keys_layout_cyrillic((KbLayout)k->letters_page) ? "АБВ" : "ABC", 1); break;
    case OSK_SHIFT: draw_icon_bg(k->shift == 2 ? ICON_SHIFT_LOCK : ICON_SHIFT, in, fg, bg); break;
    case OSK_BACKSPACE: draw_icon_bg(ICON_BACKSPACE, in, fg, bg); break;
    case OSK_ENTER: draw_icon_bg(ICON_ENTER, in, fg, bg); break;
    case OSK_LEFT: draw_icon_bg(ICON_LEFT, in, fg, bg); break;
    case OSK_RIGHT: draw_icon_bg(ICON_RIGHT, in, fg, bg); break;
    case OSK_UP: draw_icon_bg(ICON_UP, in, fg, bg); break;
    case OSK_DOWN: draw_icon_bg(ICON_DOWN, in, fg, bg); break;
    case OSK_HIDE: draw_icon_bg(ICON_HIDE, in, fg, bg); break;
    }
}

static void osk_draw_variants(Osk *k);

void osk_draw(Osk *k) {
    FillArea(k->area.x, k->area.y, k->area.w, k->area.h, WHITE);
    FillArea(k->area.x, k->area.y, k->area.w, 3, BLACK);
    for (int i = 0; i < k->nkeys; i++) osk_draw_key(k, i, 0);
    osk_draw_variants(k);
}

int osk_key_at(Osk *k, int x, int y) {
    for (int i = 0; i < k->nkeys; i++) if (rect_hit(k->rects[i], x, y)) return i;
    return -1;
}

int osk_key_repeats(Osk *k, int i) {
    if (i < 0 || i >= k->nkeys) return 0;
    OskKeyType t = k->defs[i]->type;
    return t == OSK_BACKSPACE || t == OSK_LEFT || t == OSK_RIGHT || t == OSK_UP || t == OSK_DOWN;
}

void osk_set_layout(Osk *k, int layout) {
    k->letters_page = layout >= 0 && layout < LAYOUT_COUNT ? layout : LAYOUT_EN;
    if (k->page != OSK_PAGE_SYMBOLS) k->page = k->letters_page;
    layout_keys(k);
}

int osk_auto_shift(Osk *k, int on) {
    if (k->shift == 2 || k->page == OSK_PAGE_SYMBOLS) return 0;
    int want = on ? 1 : 0;
    if (k->shift == want) return 0;
    k->shift = want;
    return 1;
}

OskResult osk_activate(Osk *k, int i, long long now_ms) {
    OskResult res = { NULL, ACT_NONE, 0 };
    if (i < 0 || i >= k->nkeys) return res;
    const OskKey *d = k->defs[i];
    switch (d->type) {
    case OSK_CHAR:
        res.text = char_label(k, d);
        if (k->shift == 1 && d->upper) { k->shift = 0; res.redraw_keyboard = 1; }
        break;
    case OSK_SPACE: res.text = " "; break;
    case OSK_ENTER: res.action = ACT_ENTER; break;
    case OSK_BACKSPACE: res.action = ACT_BACKSPACE; break;
    case OSK_LEFT: res.action = ACT_LEFT; break;
    case OSK_RIGHT: res.action = ACT_RIGHT; break;
    case OSK_UP: res.action = ACT_UP; break;
    case OSK_DOWN: res.action = ACT_DOWN; break;
    case OSK_HIDE: res.action = ACT_HIDE_KEYBOARD; break;
    case OSK_SHIFT:
        if (k->shift == 0) k->shift = 1;
        else if (k->shift == 1 && now_ms - k->last_shift_ms < 500) k->shift = 2;
        else k->shift = 0;
        k->last_shift_ms = now_ms;
        res.redraw_keyboard = 1;
        break;
    case OSK_LANG:
        k->letters_page = keys_next_layout((KbLayout)k->letters_page, g_settings.layouts);
        k->page = k->letters_page;
        res.action = ACT_TOGGLE_LAYOUT;
        res.redraw_keyboard = 1;
        layout_keys(k);
        break;
    case OSK_SYMBOLS:
        k->page = OSK_PAGE_SYMBOLS;
        k->shift = 0;
        res.redraw_keyboard = 1;
        layout_keys(k);
        break;
    case OSK_LETTERS:
        k->page = k->letters_page;
        res.redraw_keyboard = 1;
        layout_keys(k);
        break;
    }
    return res;
}

/* ---------- long-press variants ---------- */

int osk_key_has_variants(Osk *k, int i) {
    if (i < 0 || i >= k->nkeys || k->defs[i]->type != OSK_CHAR) return 0;
    const char *v[16];
    return keys_variants(char_label(k, k->defs[i]), v, 16) > 0;
}

int osk_open_variants(Osk *k, int i) {
    if (i < 0 || i >= k->nkeys || k->defs[i]->type != OSK_CHAR) return 0;
    int n = keys_variants(char_label(k, k->defs[i]), k->var, 16);
    if (n <= 0) { k->var_n = 0; return 0; }
    Rect kr = k->rects[i];
    int w = kr.w, h = kr.h;
    if (n * w > k->area.w) w = k->area.w / n;
    int x0 = kr.x + kr.w / 2 - n * w / 2;
    if (x0 + n * w > k->area.x + k->area.w) x0 = k->area.x + k->area.w - n * w;
    if (x0 < k->area.x) x0 = k->area.x;
    int y0 = kr.y - h;
    if (y0 < k->area.y + 3) y0 = k->area.y + 3;
    for (int j = 0; j < n; j++) k->var_rects[j] = rect_make(x0 + j * w, y0, w, h);
    k->var_n = n;
    k->var_key = i;
    return n;
}

void osk_close_variants(Osk *k) { k->var_n = 0; }

int osk_variant_at(Osk *k, int x, int y) {
    for (int j = 0; j < k->var_n; j++) if (rect_hit(k->var_rects[j], x, y)) return j;
    return -1;
}

static void osk_draw_variants(Osk *k) {
    for (int j = 0; j < k->var_n; j++) {
        Rect r = k->var_rects[j];
        Rect in = rect_make(r.x + KEY_GAP / 2, r.y + KEY_GAP / 2, r.w - KEY_GAP, r.h - KEY_GAP);
        FillArea(r.x, r.y, r.w, r.h, WHITE);
        FillArea(in.x, in.y, in.w, in.h, WHITE);
        DrawRect(in.x, in.y, in.w, in.h, BLACK);
        DrawRect(in.x + 1, in.y + 1, in.w - 2, in.h - 2, BLACK);
        DrawRect(in.x + 2, in.y + 2, in.w - 4, in.h - 4, BLACK);
        draw_text_fit(f_key, BLACK, in, k->var[j], 1);
    }
}

OskResult osk_pick_variant(Osk *k, int v) {
    OskResult res = { NULL, ACT_NONE, 1 };
    if (v < 0 || v >= k->var_n) return res;
    res.text = k->var[v];
    if (k->shift == 1) k->shift = 0;
    k->var_n = 0;
    return res;
}

#ifdef HOST_SIM
int osk_sim_variant_point(Osk *k, const char *text, int *x, int *y) {
    for (int j = 0; j < k->var_n; j++)
        if (!strcmp(k->var[j], text)) {
            *x = k->var_rects[j].x + k->var_rects[j].w / 2;
            *y = k->var_rects[j].y + k->var_rects[j].h / 2;
            return 1;
        }
    return 0;
}
#endif
