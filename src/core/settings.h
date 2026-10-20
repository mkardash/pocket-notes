/* App settings saved as "key=value" lines. */
#ifndef SETTINGS_H
#define SETTINGS_H

typedef struct {
    int font_size;          /* editor text size in pixels */
    char font_name[128];    /* editor font; empty = automatic */
    int layout;             /* KbLayout of the keyboard */
    int keyboard_visible;   /* on-screen keyboard shown in the editor */
    int zen;                /* distraction-free view: plain text, no toolbars */
    int indent;             /* first-line indent for prose paragraphs */
    int native_keyboard;    /* use PocketBook's own keyboard instead of the built-in one */
    int ui_lang;            /* interface language: 0 English, 1 Ukrainian */
    char last_book[1024];   /* book folder open last time */
    char last_doc[1024];    /* document open last time (empty if none) */
    int last_para, last_off;
} Settings;

void settings_defaults(Settings *s);
/* Missing file or unknown keys leave defaults in place. */
void settings_load(Settings *s, const char *path);
int settings_save(const Settings *s, const char *path);

#endif
