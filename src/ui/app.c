/* App lifecycle, screen switching, deferred repaint, and event routing. */
#include "app.h"
#include "../core/storage.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>

#ifndef APP_ROOT
#define APP_ROOT "/mnt/ext1/PocketNotes"
#endif
#ifndef APP_SETTINGS
#define APP_SETTINGS "/mnt/ext1/system/config/pocketnotes.cfg"
#endif
#define OLD_ROOT "/mnt/ext1/Writing"          /* folder used by the first test builds */

Settings g_settings;
char g_root[1024] = APP_ROOT;
static char settings_path[1024] = APP_SETTINGS;
ifont *f_ui, *f_ui_bold, *f_small, *f_key, *f_text;
ifont *f_text_b, *f_text_i, *f_text_bi, *f_mono, *f_head[6];
int SW, SH;
int g_header_h, g_row_h, g_margin;

static Screen *current = NULL;
static int pending_redraw = -1;     /* -1 none, else UPD_* */
static int switches_since_full = 0;

long long app_now_ms(void) {
#ifdef HOST_SIM
    extern long long sim_now(void);
    return sim_now();
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

void app_update(Rect r, int quality) {
    if (r.w <= 0 || r.h <= 0) return;
    switch (quality) {
    case UPD_FAST: DynamicUpdateBW(r.x, r.y, r.w, r.h); break;
    case UPD_PARTIAL: PartialUpdate(r.x, r.y, r.w, r.h); break;
    default: FullUpdate(); break;
    }
}

void app_redraw(int quality) {
    if (quality > pending_redraw) pending_redraw = quality;
}

void app_flush(void) {
    if (pending_redraw < 0 || !current) return;
    int q = pending_redraw;
    pending_redraw = -1;
    ClearScreen();
    if (current->draw) current->draw();
    if (popup_active()) popup_draw();
    if (q == UPD_FULL) FullUpdate();
    else app_update(rect_make(0, 0, SW, SH), q);
}

void app_show(Screen *s) {
    if (current && current->leave) current->leave();
    current = s;
    if (s->enter) s->enter();
    /* A full (flashing) refresh now and then clears e-ink ghosting. */
    if (++switches_since_full >= 8) { switches_since_full = 0; app_redraw(UPD_FULL); }
    else app_redraw(UPD_PARTIAL);
}

void app_save_settings(void) {
    char dir[1024];
    snprintf(dir, sizeof dir, "%s", settings_path);
    char *slash = strrchr(dir, '/');
    if (slash) { *slash = 0; mkdir(dir, 0755); }
    settings_save(&g_settings, settings_path);
}

static int find_font(const char *want) {
    char **fonts = EnumFonts();
    for (int i = 0; fonts && fonts[i]; i++) if (!strcmp(fonts[i], want)) return 1;
    return 0;
}

/* Picks a pleasant reading font for the editor if the user hasn't chosen one. */
static const char *text_font_name(void) {
    static const char *prefer[] = { "PTSerif-Regular", "PT Serif", "Georgia", "LiberationSerif",
                                    "DejaVuSerif", "NotoSerif-Regular", "Charis SIL", NULL };
    if (g_settings.font_name[0] && find_font(g_settings.font_name)) return g_settings.font_name;
    for (int i = 0; prefer[i]; i++) if (find_font(prefer[i])) return prefer[i];
    return DEFAULTFONT;
}

/* Finds a style variant of a font family: "PTSerif-Regular" -> "PTSerif-Bold", "Georgia" -> "Georgia-Bold". */
static const char *variant(const char *base, const char *style, const char *fallback) {
    static char buf[4][160];
    static int slot = 0;
    char *out = buf[slot++ % 4];
    char stem[128];
    snprintf(stem, sizeof stem, "%s", base);
    char *dash = strrchr(stem, '-');
    if (dash && (!strcmp(dash, "-Regular") || !strcmp(dash, "-Roman") || !strcmp(dash, "-Book"))) *dash = 0;
    const char *forms[] = { "%s-%s", "%s%s", "%s %s", NULL };
    for (int i = 0; forms[i]; i++) {
        snprintf(out, 160, forms[i], stem, style);
        if (find_font(out)) return out;
    }
    return fallback;
}

static void close_font(ifont **f) { if (*f) { CloseFont(*f); *f = NULL; } }

void app_reopen_text_font(void) {
    close_font(&f_text); close_font(&f_text_b); close_font(&f_text_i); close_font(&f_text_bi); close_font(&f_mono);
    for (int i = 0; i < 6; i++) close_font(&f_head[i]);
    const char *base = text_font_name();
    int size = g_settings.font_size;
    f_text = OpenFont(base, size, 1);
    const char *bold = variant(base, "Bold", DEFAULTFONTB);
    f_text_b = OpenFont(bold, size, 1);
    f_text_i = OpenFont(variant(base, "Italic", DEFAULTFONTI), size, 1);
    f_text_bi = OpenFont(variant(base, "BoldItalic", iv_get_default_font(FONT_BOLDITALIC)), size, 1);
    f_mono = OpenFont(DEFAULTFONTM, size * 9 / 10, 1);
    static const int scale[6] = { 160, 135, 118, 105, 100, 100 };
    for (int i = 0; i < 6; i++) f_head[i] = OpenFont(bold, size * scale[i] / 100, 1);
}

static void init(void) {
    SetPanelType(PANEL_DISABLED);
    SW = ScreenWidth();
    SH = ScreenHeight();
    g_header_h = SH * 7 / 100;
    g_row_h = SH * 55 / 1000;
    g_margin = SW * 3 / 100;
    settings_defaults(&g_settings);
#ifdef HOST_SIM
    {
        extern const char *sim_root(void), *sim_settings_path(void);
        snprintf(g_root, sizeof g_root, "%s", sim_root());
        snprintf(settings_path, sizeof settings_path, "%s", sim_settings_path());
    }
#endif
    settings_load(&g_settings, settings_path);
    i18n_set(g_settings.ui_lang);
    struct stat old;
    if (stat(g_root, &old) != 0 && stat(OLD_ROOT, &old) == 0) rename(OLD_ROOT, g_root);
    mkdir(g_root, 0755);

    int ui = SH * 22 / 1000;
    f_ui = OpenFont(DEFAULTFONT, ui, 1);
    f_ui_bold = OpenFont(DEFAULTFONTB, ui, 1);
    f_small = OpenFont(DEFAULTFONT, ui * 3 / 4, 1);
    f_key = OpenFont(DEFAULTFONT, ui * 115 / 100, 1);
    app_reopen_text_font();
    input_init();

    /* Pick up where the user left off. */
    struct stat st;
    if (g_settings.last_book[0] && stat(g_settings.last_book, &st) == 0) {
        /* Opening the project screen forgets the last note, so keep a copy first. */
        char doc[1024];
        int para = g_settings.last_para, off = g_settings.last_off;
        snprintf(doc, sizeof doc, "%s", g_settings.last_doc);
        book_screen_open(g_settings.last_book);
        if (doc[0] && stat(doc, &st) == 0)
            editor_open(g_settings.last_book, doc, para, off, g_settings.keyboard_visible);
    } else {
        app_show(&screen_library);
    }
    switches_since_full = 0;
    app_redraw(UPD_FULL);
}

static void save_state(void) {
    if (current && current->leave) current->leave();
    app_save_settings();
}

void app_exit(void) {
    save_state();
    CloseApp();
}

/* Text and commands from physical keyboards go to the popup first, then the screen. */
void app_deliver_text(const char *s) {
    if (popup_active()) popup_text(s);
    else if (current && current->text) current->text(s);
}

void app_deliver_action(Action a) {
    if (popup_active()) popup_action(a);
    else if (current && current->action) current->action(a);
}

static void device_key(int key) {
    if (key == IV_KEY_HOME) { app_exit(); return; }
    if (popup_active()) {
        if (key == IV_KEY_BACK || key == IV_KEY_HOME) popup_action(ACT_BACK);
        else if (key == IV_KEY_OK) popup_action(ACT_ENTER);
        return;
    }
    if (current && current->device_key) current->device_key(key);
}

int app_handler(int type, int par1, int par2) {
    int handled = 1;
    switch (type) {
    case EVT_INIT: init(); break;
    case EVT_SHOW:
    case EVT_FOREGROUND:
        app_redraw(UPD_FULL);
        break;
    case EVT_HIDE:
    case EVT_BACKGROUND:
    case EVT_SAVESTATE:
        save_state();
        break;
    case EVT_EXIT:
        save_state();
        break;
    case EVT_POINTERDOWN:
    case EVT_POINTERUP:
    case EVT_POINTERLONG:
    case EVT_POINTERMOVE:
    case EVT_POINTERDRAG:
        if (popup_active()) popup_pointer(type, par1, par2);
        else if (current && current->pointer) current->pointer(type, par1, par2);
        break;
    case EVT_KEYPRESS:
    case EVT_KEYREPEAT:
        device_key(par1);
        break;
    case EVT_KEYPRESS_EXT: if (!input_direct_active()) input_linux_key(par1, 1); break;
    case EVT_KEYREPEAT_EXT: if (!input_direct_active()) input_linux_key(par1, 2); break;
    case EVT_KEYRELEASE_EXT: if (!input_direct_active()) input_linux_key(par1, 0); break;
    case EVT_KEYRELEASE: break;
    default: handled = 0; break;
    }
    app_flush();
    return handled;
}

#ifndef HOST_SIM
int main(void) {
    InkViewMain(app_handler);
    return 0;
}
#endif
