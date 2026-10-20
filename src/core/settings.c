#include "settings.h"
#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void settings_defaults(Settings *s) {
    memset(s, 0, sizeof *s);
    s->font_size = 40;
    s->layout = 1;          /* LAYOUT_EN */
    s->keyboard_visible = 1;
    s->indent = 1;
}

static void copy_str(char *dst, size_t cap, const char *v) { snprintf(dst, cap, "%s", v); }

void settings_load(Settings *s, const char *path) {
    char *data = storage_read(path, NULL);
    if (!data) return;
    char *save = NULL;
    for (char *line = strtok_r(data, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        const char *k = line, *v = eq + 1;
        if (!strcmp(k, "font_size")) { int n = atoi(v); if (n >= 20 && n <= 96) s->font_size = n; }
        else if (!strcmp(k, "font_name")) copy_str(s->font_name, sizeof s->font_name, v);
        else if (!strcmp(k, "layout")) s->layout = atoi(v) ? 1 : 0;
        else if (!strcmp(k, "keyboard_visible")) s->keyboard_visible = atoi(v) ? 1 : 0;
        else if (!strcmp(k, "zen")) s->zen = atoi(v) ? 1 : 0;
        else if (!strcmp(k, "indent")) s->indent = atoi(v) ? 1 : 0;
        else if (!strcmp(k, "ui_lang")) s->ui_lang = atoi(v) ? 1 : 0;
        else if (!strcmp(k, "last_book")) copy_str(s->last_book, sizeof s->last_book, v);
        else if (!strcmp(k, "last_doc")) copy_str(s->last_doc, sizeof s->last_doc, v);
        else if (!strcmp(k, "last_para")) s->last_para = atoi(v);
        else if (!strcmp(k, "last_off")) s->last_off = atoi(v);
    }
    free(data);
}

int settings_save(const Settings *s, const char *path) {
    char buf[4096];
    int n = snprintf(buf, sizeof buf,
        "font_size=%d\nfont_name=%s\nlayout=%d\nkeyboard_visible=%d\nzen=%d\nindent=%d\nui_lang=%d\n"
        "last_book=%s\nlast_doc=%s\nlast_para=%d\nlast_off=%d\n",
        s->font_size, s->font_name, s->layout, s->keyboard_visible, s->zen, s->indent, s->ui_lang,
        s->last_book, s->last_doc, s->last_para, s->last_off);
    if (n < 0 || n >= (int)sizeof buf) return -1;
    return storage_write_atomic(path, buf, n);
}
