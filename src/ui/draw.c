#include "app.h"
#include "../core/utf8.h"
#include "../core/layout.h"
#include "icons_data.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int text_width(ifont *f, const char *s) {
    SetFont(f, BLACK);
    return StringWidth(s);
}

int measure_current(void *ctx, const char *s, int len) {
    char buf[2048];
    if (len <= 0) return 0;
    if (len >= (int)sizeof buf) len = sizeof buf - 1;
    memcpy(buf, s, len);
    buf[len] = 0;
    return StringWidth(buf);
}

int draw_wrapped(ifont *f, int color, Rect box, const char *text, int draw) {
    SetFont(f, color);
    int lh = f->height * 5 / 4, y = box.y;
    const char *p = text;
    LineSpan *lines = NULL;
    int cap = 0;
    while (1) {
        const char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        int n = layout_wrap(p, len, box.w, measure_current, NULL, &lines, &cap);
        for (int i = 0; i < n; i++) {
            if (draw) {
                char buf[1024];
                int l = lines[i].len < 1023 ? lines[i].len : 1023;
                memcpy(buf, p + lines[i].start, l);
                buf[l] = 0;
                DrawString(box.x, y, buf);
            }
            y += lh;
        }
        if (!nl) break;
        p = nl + 1;
    }
    free(lines);
    return y - box.y;
}

void draw_text_fit(ifont *f, int color, Rect box, const char *s, int align) {
    char buf[1024];
    snprintf(buf, sizeof buf, "%s", s);
    SetFont(f, color);
    int w = StringWidth(buf);
    if (w > box.w) {
        /* Drop characters from the end until "text…" fits. */
        int len = (int)strlen(buf);
        while (len > 0) {
            len = utf8_prev(buf, len);
            memcpy(buf + len, "…", 4);
            if (StringWidth(buf) <= box.w) break;
        }
        w = StringWidth(buf);
    }
    int x = box.x;
    if (align == 1) x = box.x + (box.w - w) / 2;
    else if (align == 2) x = box.x + box.w - w;
    int y = box.y + (box.h - f->height) / 2;
    DrawString(x, y, buf);
}

static void frame(Rect r, int t, int color) {
    FillArea(r.x, r.y, r.w, t, color);
    FillArea(r.x, r.y + r.h - t, r.w, t, color);
    FillArea(r.x, r.y, t, r.h, color);
    FillArea(r.x + r.w - t, r.y, t, r.h, color);
}

void draw_button(Rect r, const char *label, int filled) {
    if (filled) {
        FillArea(r.x, r.y, r.w, r.h, BLACK);
        draw_text_fit(f_ui_bold, WHITE, rect_make(r.x + 12, r.y, r.w - 24, r.h), label, 1);
    } else {
        FillArea(r.x, r.y, r.w, r.h, WHITE);
        frame(r, 3, BLACK);
        draw_text_fit(f_ui, BLACK, rect_make(r.x + 12, r.y, r.w - 24, r.h), label, 1);
    }
}

Rect header_back_rect(void) { return rect_make(0, 0, g_header_h + 10, g_header_h); }
Rect header_slot(int i) { return rect_make(SW - (i + 1) * g_header_h, 0, g_header_h, g_header_h); }

void draw_header(const char *title, int left_icon, int right_slots) {
    FillArea(0, 0, SW, g_header_h, WHITE);
    int x = g_margin;
    if (left_icon >= 0) {
        draw_icon((IconId)left_icon, header_back_rect(), BLACK);
        x = header_back_rect().w;
    }
    draw_text_fit(f_ui_bold, BLACK, rect_make(x, 0, SW - x - right_slots * g_header_h - 10, g_header_h), title, 0);
    FillArea(0, g_header_h - 3, SW, 3, BLACK);
}

static int gray_of(int color) {
    int r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;
    return (r * 299 + g * 587 + b * 114) / 1000;
}

static int bitmap_index(IconId id) {
    switch (id) {
    case ICON_BACK: return GI_BACK;
    case ICON_KEYBOARD: return GI_KEYBOARD;
    case ICON_PLUS: return GI_PLUS;
    case ICON_BULB: return GI_BULB;
    case ICON_DOTS: return GI_DOTS;
    case ICON_TEXT: return GI_TEXT;
    case ICON_FOLDER: return GI_FOLDER;
    case ICON_DOC: return GI_DOC;
    case ICON_LEFT: return GI_LEFT;
    case ICON_RIGHT: return GI_RIGHT;
    case ICON_UP: return GI_UP;
    case ICON_DOWN: return GI_DOWN;
    case ICON_SHIFT: return GI_SHIFT;
    case ICON_SHIFT_LOCK: return GI_SHIFT_LOCK;
    case ICON_BACKSPACE: return GI_BACKSPACE;
    case ICON_ENTER: return GI_ENTER;
    case ICON_HIDE: return GI_HIDE;
    case ICON_CLOSE: return GI_CLOSE;
    case ICON_CHEVRON_DOWN: return GI_CHEVRON_DOWN;
    case ICON_CHEVRON_RIGHT: return GI_CHEVRON_RIGHT;
    case ICON_BOOK: return GI_BOOK;
    case ICON_SETTINGS: return GI_SETTINGS;
    case ICON_FORMAT: return GI_FORMAT;
    case ICON_ZEN: return GI_ZEN;
    case ICON_ZEN_EXIT: return GI_ZEN_EXIT;
    case ICON_SOURCE: return GI_SOURCE;
    }
    return GI_DOTS;
}

void draw_icon_bg(IconId id, Rect r, int color, int bg) {
    /* Icons take about 60% of the box; pick the closest pre-rendered size. */
    int want = (r.w < r.h ? r.w : r.h) * 60 / 100;
    const IconBitmap *set = icon_bitmaps[bitmap_index(id)];
    const IconBitmap *bm = &set[0];
    for (int i = 0; i < ICON_SIZES; i++)
        if (abs(set[i].size - want) < abs(bm->size - want)) bm = &set[i];
    int n = bm->size;
    int x0 = r.x + (r.w - n) / 2, y0 = r.y + (r.h - n) / 2;
    int fg = gray_of(color), bk = gray_of(bg);
    /* Blend each pixel onto the known background; same-shade runs become one FillArea. */
    for (int y = 0; y < n; y++) {
        const unsigned char *row = bm->alpha + y * n;
        int x = 0;
        while (x < n) {
            int a = row[x];
            if (!a) { x++; continue; }
            int v = (bk * (255 - a) + fg * a) / 255;
            int start = x;
            while (x < n && row[x] && (bk * (255 - row[x]) + fg * row[x]) / 255 == v) x++;
            FillArea(x0 + start, y0 + y, x - start, 1, v * 0x010101);
        }
    }
}

void draw_icon(IconId id, Rect r, int color) {
    draw_icon_bg(id, r, color, WHITE);
}
