/* Physical keyboards: turns Linux key codes into text and editing commands.
 * Key codes arrive from InkView (EVT_KEYPRESS_EXT) or, when the system allows it,
 * straight from /dev/input. */
#include "app.h"
#include <stdio.h>
#include <string.h>

void app_deliver_text(const char *s);
void app_deliver_action(Action a);

static struct {
    int shift_l, shift_r, ctrl, alt, meta, caps;
    int seen;               /* a physical key arrived at least once */
} K;

int input_physical_seen(void) { return K.seen; }

static int is_letter(const char *s) {
    unsigned char c = (unsigned char)s[0];
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == 0xD0 || c == 0xD1 || c == 0xD2;
}

static void toggle_layout(void) {
    g_settings.layout = g_settings.layout == LAYOUT_UK ? LAYOUT_EN : LAYOUT_UK;
    app_deliver_action(ACT_TOGGLE_LAYOUT);
}

void input_linux_key(int code, int value) {
    K.seen = 1;
    int down = value != 0;
    switch (code) {
    case KC_LEFTSHIFT:
    case KC_RIGHTSHIFT:
        if (value == 1 && (K.alt || K.ctrl)) toggle_layout();   /* Alt+Shift / Ctrl+Shift */
        if (code == KC_LEFTSHIFT) K.shift_l = down; else K.shift_r = down;
        return;
    case KC_LEFTCTRL: case KC_RIGHTCTRL: K.ctrl = down; return;
    case KC_LEFTALT: case KC_RIGHTALT:
        if (value == 1 && (K.shift_l || K.shift_r)) toggle_layout();
        K.alt = down;
        return;
    case KC_LEFTMETA: case KC_RIGHTMETA: K.meta = down; return;
    case KC_CAPSLOCK: if (value == 1) K.caps = !K.caps; return;
    }
    if (!down) return;
    int shift = K.shift_l || K.shift_r;

    if ((K.ctrl || K.meta) && code == KC_SPACE) { if (value == 1) toggle_layout(); return; }
    if ((K.ctrl || K.meta) && code == KC_S) { app_deliver_action(ACT_SAVE); return; }

    Action a = ACT_NONE;
    switch (code) {
    case KC_BACKSPACE: a = ACT_BACKSPACE; break;
    case KC_DELETE: a = ACT_DELETE; break;
    case KC_ENTER: case KC_KPENTER: a = ACT_ENTER; break;
    case KC_LEFT: a = ACT_LEFT; break;
    case KC_RIGHT: a = ACT_RIGHT; break;
    case KC_UP: a = ACT_UP; break;
    case KC_DOWN: a = ACT_DOWN; break;
    case KC_HOME: a = ACT_HOME; break;
    case KC_END: a = ACT_END; break;
    case KC_PAGEUP: a = ACT_PAGE_UP; break;
    case KC_PAGEDOWN: a = ACT_PAGE_DOWN; break;
    case KC_ESC: a = ACT_BACK; break;
    }
    if (a != ACT_NONE) { app_deliver_action(a); return; }
    if (K.alt && !K.ctrl && !K.meta) {
        const char *t = keys_alt_char(code);
        if (t) app_deliver_text(t);
        return;
    }
    if (K.ctrl || K.alt || K.meta) return;      /* other shortcuts: ignore */
    if (code == KC_TAB) { app_deliver_text("    "); return; }

    const char *lower = keys_physical_char(code, g_settings.layout, 0);
    if (!lower) return;
    int upper = shift;
    if (K.caps && is_letter(lower)) upper = !upper;
    app_deliver_text(keys_physical_char(code, g_settings.layout, upper));
}

/* ---- direct /dev/input reading (works only where the app may open input devices) ---- */
#ifdef __linux__
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_KB 4
static int kb_fds[MAX_KB];
static int kb_count = 0;

static int has_bit(const unsigned char *bits, int bit) { return bits[bit / 8] & (1 << (bit % 8)); }

static void scan_keyboards(void) {
    DIR *d = opendir("/dev/input");
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) && kb_count < MAX_KB) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[64];
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        unsigned char bits[KEY_MAX / 8 + 1];
        memset(bits, 0, sizeof bits);
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof bits), bits) >= 0 && has_bit(bits, KEY_A) && has_bit(bits, KEY_Z))
            kb_fds[kb_count++] = fd;
        else
            close(fd);
    }
    closedir(d);
}

static void poll_keyboards(void) {
    for (int i = 0; i < kb_count; i++) {
        struct input_event ev;
        ssize_t n;
        while ((n = read(kb_fds[i], &ev, sizeof ev)) == (ssize_t)sizeof ev) {
            if (ev.type == EV_KEY) input_linux_key(ev.code, ev.value);
        }
    }
    app_flush();
    SetHardTimer("pbw_kbpoll", poll_keyboards, 30);
}

void input_init(void) {
    scan_keyboards();
    if (kb_count > 0) SetHardTimer("pbw_kbpoll", poll_keyboards, 30);
}

int input_direct_active(void) { return kb_count > 0; }
#else
void input_init(void) {}
int input_direct_active(void) { return 0; }
#endif
