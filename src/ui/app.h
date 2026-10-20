/* Shared UI state and helpers: screens, popups, fonts, drawing, screen refresh. */
#ifndef APP_H
#define APP_H
#include <inkview.h>
#include "../core/settings.h"
#include "../core/keys.h"
#include "i18n.h"

typedef struct { int x, y, w, h; } Rect;
static inline int rect_hit(Rect r, int x, int y) { return x >= r.x && y >= r.y && x < r.x + r.w && y < r.y + r.h; }
static inline Rect rect_make(int x, int y, int w, int h) { Rect r = { x, y, w, h }; return r; }

/* Editing commands shared by the on-screen keyboard and physical keyboards. */
typedef enum {
    ACT_NONE, ACT_BACKSPACE, ACT_DELETE, ACT_ENTER, ACT_LEFT, ACT_RIGHT, ACT_UP, ACT_DOWN,
    ACT_HOME, ACT_END, ACT_PAGE_UP, ACT_PAGE_DOWN, ACT_SAVE, ACT_BACK, ACT_TOGGLE_LAYOUT, ACT_HIDE_KEYBOARD
} Action;

/* A full-screen view. Every callback is optional. */
typedef struct Screen {
    void (*enter)(void);
    void (*draw)(void);                         /* paint everything into the framebuffer */
    void (*pointer)(int type, int x, int y);    /* EVT_POINTERDOWN/UP/LONG/MOVE */
    void (*device_key)(int key);                /* PocketBook buttons (IV_KEY_*) */
    void (*text)(const char *utf8);             /* text typed on a physical keyboard */
    void (*action)(Action a);
    void (*leave)(void);                        /* save state before switching away or exiting */
} Screen;

/* Screen refresh quality. */
enum { UPD_FAST, UPD_PARTIAL, UPD_FULL };

/* ---- app.c ---- */
extern Settings g_settings;
extern char g_root[1024];               /* folder that holds the books */
extern ifont *f_ui, *f_ui_bold, *f_small, *f_key, *f_text;
/* Text fonts for formatted notes, all in the reading font's family. */
extern ifont *f_text_b, *f_text_i, *f_text_bi, *f_mono, *f_head[6];
extern int SW, SH;                      /* screen size */
extern int g_header_h, g_row_h, g_margin;

void app_show(Screen *s);
/* Schedules a repaint of the whole screen (and popup) at the end of the current event. */
void app_redraw(int quality);
/* Performs any scheduled repaint now. Called after every event and timer. */
void app_flush(void);
/* Sends a framebuffer region to the e-ink panel. */
void app_update(Rect r, int quality);
void app_save_settings(void);
void app_reopen_text_font(void);
void app_exit(void);
long long app_now_ms(void);

/* ---- draw.c ---- */
int text_width(ifont *f, const char *s);
/* layout.h measure function for the font currently selected with SetFont. */
int measure_current(void *ctx, const char *s, int len);
/* Draws word-wrapped text; returns the height used. Pass draw=0 to only measure. */
int draw_wrapped(ifont *f, int color, Rect box, const char *text, int draw);
/* Draws s in the box, shortened with "…" if too wide. align: 0 left, 1 center, 2 right. */
void draw_text_fit(ifont *f, int color, Rect box, const char *s, int align);
void draw_button(Rect r, const char *label, int filled);
/* left_icon: an IconId shown at the left (back/close), or -1 for none. */
void draw_header(const char *title, int left_icon);
Rect header_back_rect(void);
/* Icon buttons on the right side of the header, counted from the right edge (0 = rightmost). */
Rect header_slot(int index_from_right);
typedef enum { ICON_BACK, ICON_KEYBOARD, ICON_PLUS, ICON_BULB, ICON_DOTS, ICON_TEXT, ICON_FOLDER,
               ICON_DOC, ICON_LEFT, ICON_RIGHT, ICON_UP, ICON_DOWN, ICON_SHIFT, ICON_SHIFT_LOCK,
               ICON_BACKSPACE, ICON_ENTER, ICON_HIDE, ICON_CLOSE, ICON_CHEVRON_DOWN, ICON_CHEVRON_RIGHT,
               ICON_BOOK, ICON_SETTINGS, ICON_FORMAT, ICON_ZEN, ICON_ZEN_EXIT } IconId;
/* Draws a Lucide icon centred in r on a white background. */
void draw_icon(IconId id, Rect r, int color);
/* Same, on a known background colour (keys, pressed buttons). */
void draw_icon_bg(IconId id, Rect r, int color, int bg);

/* ---- popup.c: modal dialogs drawn over the current screen ---- */
typedef void (*menu_cb)(int index, void *ctx);          /* index -1 = cancelled */
typedef void (*confirm_cb)(int ok, void *ctx);
typedef void (*prompt_cb)(const char *text, void *ctx);  /* text NULL = cancelled */
void popup_menu(const char *title, const char **items, int n, menu_cb cb, void *ctx);
void popup_confirm(const char *text, const char *ok_label, confirm_cb cb, void *ctx);
void popup_prompt(const char *title, const char *initial, prompt_cb cb, void *ctx);
void popup_message(const char *text);
int popup_active(void);
void popup_draw(void);
void popup_pointer(int type, int x, int y);
void popup_text(const char *utf8);
void popup_action(Action a);

/* ---- screens ---- */
extern Screen screen_library, screen_book, screen_editor;
void book_screen_open(const char *book_dir);
void editor_open(const char *book_dir, const char *doc_path, int para, int off, int show_keyboard);

/* ---- input.c: physical keyboards ---- */
void input_init(void);
/* Feeds a Linux key code event (value: 1 press, 0 release, 2 repeat). */
void input_linux_key(int code, int value);
int input_physical_seen(void);
/* 1 when keyboards are read straight from /dev/input (then InkView key events are ignored). */
int input_direct_active(void);

#endif
