/* PocketBook's own keyboard (InkView OpenKeyboard).
 * The documented mode returns the typed text when the keyboard closes. The keyboard can also
 * report text as it is typed (setKeyboardTextChangeCallback, undocumented); every callback is
 * logged (sizes and positions only, never the text) so its behaviour can be studied on a device. */
#ifndef NATIVE_KBD_H
#define NATIVE_KBD_H

typedef struct {
    void (*insert)(const char *text);   /* add text at the cursor */
    void (*erase)(int chars);           /* delete characters before the cursor */
    void (*closed)(void);               /* keyboard gone: repaint */
} NativeKbdOps;

void native_kbd_open(const char *title, NativeKbdOps ops);
int native_kbd_is_open(void);

#endif
