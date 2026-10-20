/* On-screen keyboard docked at the bottom of the screen. */
#ifndef OSK_H
#define OSK_H
#include "app.h"

#define OSK_MAX_KEYS 96

typedef struct {
    Rect area;
    int page;               /* OSK_PAGE_UK / EN / SYMBOLS */
    int letters_page;       /* letters page to return to from symbols */
    int shift;              /* 0 off, 1 next letter only, 2 caps lock */
    long long last_shift_ms;
    int nkeys;
    Rect rects[OSK_MAX_KEYS];
    const OskKey *defs[OSK_MAX_KEYS];
} Osk;

typedef struct {
    const char *text;       /* text to insert, or NULL */
    Action action;          /* editing command, or ACT_NONE */
    int redraw_keyboard;    /* labels or page changed */
} OskResult;

int osk_height(void);
void osk_init(Osk *k, Rect area, int layout);
void osk_set_area(Osk *k, Rect area);
void osk_draw(Osk *k);
int osk_key_at(Osk *k, int x, int y);
void osk_draw_key(Osk *k, int i, int pressed);
OskResult osk_activate(Osk *k, int i, long long now_ms);
/* Keys that repeat while held (backspace, arrows). */
int osk_key_repeats(Osk *k, int i);
/* Switches the letters page to a KbLayout (keeps the symbols page if it is showing). */
void osk_set_layout(Osk *k, int layout);
/* Turns on one-shot Shift (used for capitals at the start of sentences). Returns 1 if it changed. */
int osk_auto_shift(Osk *k, int on);

#endif
