/* Modal popups drawn over the current screen: menu, confirmation, name entry, message. */
#include "app.h"
#include "osk.h"
#include "../core/utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { POP_NONE, POP_MENU, POP_CONFIRM, POP_PROMPT, POP_MESSAGE };

#define MAX_ITEMS 18

static struct {
    int type;
    char title[512];
    char items[MAX_ITEMS][128];
    int n;
    menu_cb on_menu;
    confirm_cb on_confirm;
    prompt_cb on_prompt;
    void *ctx;
    char ok_label[64];
    char text[512];             /* prompt input */
    Osk osk;
    int pressed_key;
    int got_down;               /* a touch started inside this popup (ignore stray lifts) */
    Rect box, ok_btn, cancel_btn, field;
    Rect item_rects[MAX_ITEMS];
} P;

int popup_active(void) { return P.type != POP_NONE; }

static void open_popup(int type, const char *title, void *ctx) {
    P.type = type;
    snprintf(P.title, sizeof P.title, "%s", title ? title : "");
    P.ctx = ctx;
    P.pressed_key = -1;
    P.got_down = 0;
    app_redraw(UPD_PARTIAL);
}

void popup_menu(const char *title, const char **items, int n, menu_cb cb, void *ctx) {
    open_popup(POP_MENU, title, ctx);
    P.n = n > MAX_ITEMS ? MAX_ITEMS : n;
    for (int i = 0; i < P.n; i++) snprintf(P.items[i], sizeof P.items[i], "%s", items[i]);
    P.on_menu = cb;
}

void popup_confirm(const char *text, const char *ok_label, confirm_cb cb, void *ctx) {
    open_popup(POP_CONFIRM, text, ctx);
    snprintf(P.ok_label, sizeof P.ok_label, "%s", ok_label);
    P.on_confirm = cb;
}

void popup_prompt(const char *title, const char *initial, prompt_cb cb, void *ctx) {
    open_popup(POP_PROMPT, title, ctx);
    snprintf(P.text, sizeof P.text, "%s", initial ? initial : "");
    P.on_prompt = cb;
    int kh = osk_height();
    osk_init(&P.osk, rect_make(0, SH - kh, SW, kh), g_settings.layout);
    if (!P.text[0]) osk_auto_shift(&P.osk, 1);
}

void popup_message(const char *text) {
    open_popup(POP_MESSAGE, text, NULL);
}

static void close_popup(void) {
    P.type = POP_NONE;
    app_redraw(UPD_PARTIAL);
}

static void box_frame(Rect r) {
    FillArea(r.x, r.y, r.w, r.h, WHITE);
    for (int i = 0; i < 4; i++) DrawRect(r.x + i, r.y + i, r.w - 2 * i, r.h - 2 * i, BLACK);
}

static void draw_menu(void) {
    /* Long menus (the formatting menu) use two columns, filled top to bottom. */
    int cols = P.n > 9 ? 2 : 1;
    int rows = (P.n + cols - 1) / cols;
    int w = SW * (cols == 2 ? 94 : 82) / 100, pad = 24;
    int title_h = P.title[0] ? g_row_h : 0;
    int h = title_h + rows * g_row_h + 2 * pad;
    P.box = rect_make((SW - w) / 2, (SH - h) / 2, w, h);
    box_frame(P.box);
    int y0 = P.box.y + pad;
    if (title_h) {
        draw_text_fit(f_ui_bold, BLACK, rect_make(P.box.x + pad, y0, w - 2 * pad, g_row_h), P.title, 0);
        y0 += title_h;
        FillArea(P.box.x + pad, y0 - 2, w - 2 * pad, 2, DGRAY);
    }
    int col_w = (w - 2 * pad) / cols;
    for (int i = 0; i < P.n; i++) {
        int c = i / rows, r = i % rows;
        int x = P.box.x + pad + c * col_w, y = y0 + r * g_row_h;
        P.item_rects[i] = rect_make(x, y, col_w, g_row_h);
        draw_text_fit(f_ui, BLACK, rect_make(x + 16, y, col_w - 32, g_row_h), P.items[i], 0);
        if (r + 1 < rows) FillArea(x, y + g_row_h - 1, col_w - (c + 1 < cols ? 12 : 0), 1, LGRAY);
    }
    if (cols == 2) FillArea(P.box.x + pad + col_w - 1, y0 + 8, 1, rows * g_row_h - 16, LGRAY);
}

static void draw_dialog(int with_cancel, const char *ok) {
    int w = SW * 82 / 100, pad = 36;
    int text_h = draw_wrapped(f_ui, BLACK, rect_make(0, 0, w - 2 * pad, 0), P.title, 0);
    int btn_h = g_row_h;
    int h = pad + text_h + pad + btn_h + pad;
    P.box = rect_make((SW - w) / 2, (SH - h) / 2, w, h);
    box_frame(P.box);
    draw_wrapped(f_ui, BLACK, rect_make(P.box.x + pad, P.box.y + pad, w - 2 * pad, text_h), P.title, 1);
    int by = P.box.y + h - pad - btn_h;
    if (with_cancel) {
        int bw = (w - 3 * pad) / 2;
        P.cancel_btn = rect_make(P.box.x + pad, by, bw, btn_h);
        P.ok_btn = rect_make(P.box.x + 2 * pad + bw, by, bw, btn_h);
        draw_button(P.cancel_btn, T(S_CANCEL), 0);
    } else {
        P.cancel_btn = rect_make(0, 0, 0, 0);
        P.ok_btn = rect_make(P.box.x + w / 3, by, w / 3, btn_h);
    }
    draw_button(P.ok_btn, ok, 1);
}

static void draw_prompt_field(void) {
    Rect f = P.field;
    FillArea(f.x, f.y, f.w, f.h, WHITE);
    for (int i = 0; i < 3; i++) DrawRect(f.x + i, f.y + i, f.w - 2 * i, f.h - 2 * i, BLACK);
    SetFont(f_ui, BLACK);
    /* Show the end of the text if it doesn't fit. */
    const char *s = P.text;
    int maxw = f.w - 40;
    while (*s && StringWidth(s) > maxw) s += utf8_next(s, (int)strlen(s), 0);
    int tx = f.x + 20, ty = f.y + (f.h - f_ui->height) / 2;
    DrawString(tx, ty, s);
    int cx = tx + StringWidth(s) + 2;
    FillArea(cx, ty, 3, f_ui->height, BLACK);
}

static void draw_prompt(void) {
    int pad = 32;
    FillArea(0, 0, SW, SH, WHITE);
    int top = pad;
    int h = pad + g_row_h + 16 + g_row_h + pad + g_row_h + pad;
    P.box = rect_make(pad, top, SW - 2 * pad, h);
    box_frame(P.box);
    int y = P.box.y + pad;
    draw_text_fit(f_ui_bold, BLACK, rect_make(P.box.x + pad, y, P.box.w - 2 * pad, g_row_h), P.title, 0);
    y += g_row_h + 16;
    P.field = rect_make(P.box.x + pad, y, P.box.w - 2 * pad, g_row_h);
    draw_prompt_field();
    y += g_row_h + pad;
    int bw = (P.box.w - 3 * pad) / 2;
    P.cancel_btn = rect_make(P.box.x + pad, y, bw, g_row_h);
    P.ok_btn = rect_make(P.box.x + 2 * pad + bw, y, bw, g_row_h);
    draw_button(P.cancel_btn, T(S_CANCEL), 0);
    draw_button(P.ok_btn, T(S_DONE), 1);
    osk_draw(&P.osk);
}

void popup_draw(void) {
    switch (P.type) {
    case POP_MENU: draw_menu(); break;
    case POP_CONFIRM: draw_dialog(1, P.ok_label); break;
    case POP_MESSAGE: draw_dialog(0, "OK"); break;
    case POP_PROMPT: draw_prompt(); break;
    }
}

static void finish_menu(int idx) {
    menu_cb cb = P.on_menu;
    void *ctx = P.ctx;
    close_popup();
    if (cb) cb(idx, ctx);
}

static void finish_confirm(int ok) {
    confirm_cb cb = P.on_confirm;
    void *ctx = P.ctx;
    close_popup();
    if (cb) cb(ok, ctx);
}

static void finish_prompt(int ok) {
    prompt_cb cb = P.on_prompt;
    void *ctx = P.ctx;
    char text[512];
    snprintf(text, sizeof text, "%s", P.text);
    close_popup();
    if (cb) cb(ok ? text : NULL, ctx);
}

static void prompt_changed(void) {
    draw_prompt_field();
    app_update(P.field, UPD_FAST);
}

void popup_text(const char *utf8) {
    if (P.type != POP_PROMPT) return;
    for (const char *c = utf8; *c; c++) if (*c == '\n') { finish_prompt(1); return; }
    size_t len = strlen(P.text), add = strlen(utf8);
    if (len + add + 1 >= sizeof P.text) return;
    memcpy(P.text + len, utf8, add + 1);
    prompt_changed();
    if (osk_auto_shift(&P.osk, 0)) { osk_draw(&P.osk); app_update(P.osk.area, UPD_FAST); }
}

void popup_action(Action a) {
    switch (P.type) {
    case POP_PROMPT:
        if (a == ACT_BACKSPACE) {
            int len = (int)strlen(P.text);
            if (len > 0) { P.text[utf8_prev(P.text, len)] = 0; prompt_changed(); }
        } else if (a == ACT_ENTER) finish_prompt(1);
        else if (a == ACT_BACK) finish_prompt(0);
        break;
    case POP_MENU:
        if (a == ACT_BACK) finish_menu(-1);
        break;
    case POP_CONFIRM:
        if (a == ACT_BACK) finish_confirm(0);
        else if (a == ACT_ENTER) finish_confirm(1);
        break;
    case POP_MESSAGE:
        if (a == ACT_BACK || a == ACT_ENTER) close_popup();
        break;
    }
}

void popup_pointer(int type, int x, int y) {
    if (type == EVT_POINTERDOWN) P.got_down = 1;
    if (type == EVT_POINTERUP && !P.got_down) return;
    if (P.type == POP_PROMPT) {
        int k = osk_key_at(&P.osk, x, y);
        if (type == EVT_POINTERDOWN) {
            if (k >= 0) {
                P.pressed_key = k;
                osk_draw_key(&P.osk, k, 1);
                app_update(P.osk.rects[k], UPD_FAST);
            }
            return;
        }
        if (type != EVT_POINTERUP) return;
        if (P.pressed_key >= 0) {
            int pk = P.pressed_key;
            P.pressed_key = -1;
            osk_draw_key(&P.osk, pk, 0);
            app_update(P.osk.rects[pk], UPD_FAST);
            if (pk != k) return;
            OskResult r = osk_activate(&P.osk, k, app_now_ms());
            if (r.redraw_keyboard) { osk_draw(&P.osk); app_update(P.osk.area, UPD_FAST); }
            if (r.action == ACT_TOGGLE_LAYOUT) {
                g_settings.layout = P.osk.letters_page == OSK_PAGE_EN ? LAYOUT_EN : LAYOUT_UK;
            } else if (r.action == ACT_HIDE_KEYBOARD) {
                /* The name field needs the keyboard; treat hide as cancel. */
                finish_prompt(0);
            } else if (r.action != ACT_NONE) {
                popup_action(r.action);
            }
            if (r.text && P.type == POP_PROMPT) popup_text(r.text);
            return;
        }
        if (rect_hit(P.ok_btn, x, y)) finish_prompt(1);
        else if (rect_hit(P.cancel_btn, x, y)) finish_prompt(0);
        return;
    }
    if (type != EVT_POINTERUP) return;
    switch (P.type) {
    case POP_MENU:
        for (int i = 0; i < P.n; i++) if (rect_hit(P.item_rects[i], x, y)) { finish_menu(i); return; }
        if (!rect_hit(P.box, x, y)) finish_menu(-1);
        break;
    case POP_CONFIRM:
        if (rect_hit(P.ok_btn, x, y)) finish_confirm(1);
        else if (rect_hit(P.cancel_btn, x, y) || !rect_hit(P.box, x, y)) finish_confirm(0);
        break;
    case POP_MESSAGE:
        close_popup();
        break;
    }
}

#ifdef HOST_SIM
/* Simulator helper: centre of a menu item, found by its text. Returns 0 if not found. */
int popup_sim_item(const char *text, int *x, int *y) {
    for (int i = 0; i < P.n; i++)
        if (!strcmp(P.items[i], text)) {
            *x = P.item_rects[i].x + P.item_rects[i].w / 2;
            *y = P.item_rects[i].y + P.item_rects[i].h / 2;
            return 1;
        }
    return 0;
}
#endif
