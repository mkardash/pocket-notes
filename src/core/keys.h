/* Keyboard layouts: physical keyboard key codes (Linux input codes) and on-screen key rows. */
#ifndef KEYS_H
#define KEYS_H

typedef enum { LAYOUT_UK = 0, LAYOUT_EN = 1, LAYOUT_COUNT = 2 } KbLayout;

/* Linux input key codes used for editing (from linux/input-event-codes.h). */
enum {
    KC_ESC = 1, KC_BACKSPACE = 14, KC_TAB = 15, KC_ENTER = 28, KC_LEFTCTRL = 29,
    KC_LEFTSHIFT = 42, KC_RIGHTSHIFT = 54, KC_LEFTALT = 56, KC_SPACE = 57, KC_CAPSLOCK = 58,
    KC_KPENTER = 96, KC_RIGHTCTRL = 97, KC_RIGHTALT = 100, KC_HOME = 102, KC_UP = 103,
    KC_PAGEUP = 104, KC_LEFT = 105, KC_RIGHT = 106, KC_END = 107, KC_DOWN = 108,
    KC_PAGEDOWN = 109, KC_DELETE = 111, KC_LEFTMETA = 125, KC_RIGHTMETA = 126,
    KC_S = 31
};

/* UTF-8 text typed by a key code in a layout, or NULL if the key types nothing. */
const char *keys_physical_char(int keycode, KbLayout layout, int shift);

/* Typographic characters typed with Alt + key (same in every layout): - — , « . » / … ' ’ */
const char *keys_alt_char(int keycode);

/* On-screen keyboard. */
typedef enum {
    OSK_CHAR, OSK_SHIFT, OSK_BACKSPACE, OSK_ENTER, OSK_SPACE, OSK_LANG, OSK_SYMBOLS,
    OSK_LETTERS, OSK_LEFT, OSK_RIGHT, OSK_UP, OSK_DOWN, OSK_HIDE
} OskKeyType;

typedef struct {
    OskKeyType type;
    const char *lower;   /* text typed (OSK_CHAR) */
    const char *upper;   /* text typed with Shift; NULL = same as lower */
    int weight;          /* width units; every row adds up to 120 */
} OskKey;

typedef struct { const OskKey *keys; int n; } OskRow;
typedef struct { const OskRow *rows; int nrows; } OskLayout;

enum { OSK_PAGE_UK = 0, OSK_PAGE_EN = 1, OSK_PAGE_SYMBOLS = 2 };
const OskLayout *keys_osk_page(int page);

#endif
