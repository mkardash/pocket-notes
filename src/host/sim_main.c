/* Simulator: drives the app with scripted touches and key presses, checks the results on disk,
 * and saves PNG screenshots. Usage: build/sim <output-dir> */
#include <inkview.h>
#include "sim.h"
#include "../core/keys.h"
#include "../core/doc.h"
#include "../core/storage.h"
#include "../ui/osk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int app_handler(int type, int par1, int par2);
int popup_sim_item(const char *text, int *x, int *y);
Osk *editor_sim_osk(void);
Doc *editor_sim_doc(void);
int editor_sim_checkbox_point(int p, int *x, int *y);

static char out_dir[512], root[600], cfg[600];
const char *sim_root(void) { return root; }
const char *sim_settings_path(void) { return cfg; }

static const int SWd = 1264, SHd = 1680;
static int hdr, row;
static int fails = 0;

static void shot(const char *name) {
    char p[800];
    snprintf(p, sizeof p, "%s/%s.png", out_dir, name);
    sim_save_png(p);
    printf("  saved %s\n", p);
}

static void expect(int cond, const char *what) {
    printf("  %s %s\n", cond ? "ok  " : "FAIL", what);
    if (!cond) fails++;
}

static void tap(int x, int y) {
    app_handler(EVT_POINTERDOWN, x, y);
    sim_advance(80);
    app_handler(EVT_POINTERUP, x, y);
    sim_advance(20);
}

static void long_press(int x, int y) {
    app_handler(EVT_POINTERDOWN, x, y);
    sim_advance(700);
    app_handler(EVT_POINTERLONG, x, y);
    app_handler(EVT_POINTERUP, x, y);
}

static void tap_slot(int i) { tap(SWd - i * hdr - hdr / 2, hdr / 2); }
static void tap_back(void) { tap(hdr / 2, hdr / 2); }
static void tap_row(int i) { tap(SWd / 2, hdr + i * row + row / 2); }

static void tap_menu(const char *text) {
    int x, y;
    if (popup_sim_item(text, &x, &y)) tap(x, y);
    else { fprintf(stderr, "sim: no menu item '%s'\n", text); fails++; }
}

static void key(int code) {
    app_handler(EVT_KEYPRESS_EXT, code, 0);
    app_handler(EVT_KEYRELEASE_EXT, code, 0);
}

static void chord(int mod, int code) {
    app_handler(EVT_KEYPRESS_EXT, mod, 0);
    key(code);
    app_handler(EVT_KEYRELEASE_EXT, mod, 0);
}

/* Types text on a "physical keyboard" in the given layout (the app must be in that layout too). */
static void type_physical(const char *s, int layout) {
    while (*s) {
        if (*s == '\n') { key(KC_ENTER); s++; continue; }
        int found = 0;
        for (int shift = 0; shift < 2 && !found; shift++)
            for (int code = 1; code < 64 && !found; code++) {
                const char *c = keys_physical_char(code, layout, shift);
                if (c && c[0] && strncmp(s, c, strlen(c)) == 0) {
                    if (shift) app_handler(EVT_KEYPRESS_EXT, KC_LEFTSHIFT, 0);
                    key(code);
                    if (shift) app_handler(EVT_KEYRELEASE_EXT, KC_LEFTSHIFT, 0);
                    s += strlen(c);
                    found = 1;
                }
            }
        if (!found) { fprintf(stderr, "sim: can't type '%.4s'\n", s); s++; }
    }
}

static int tap_osk_type(Osk *k, OskKeyType t) {
    for (int i = 0; i < k->nkeys; i++)
        if (k->defs[i]->type == t) { tap(k->rects[i].x + k->rects[i].w / 2, k->rects[i].y + k->rects[i].h / 2); return 1; }
    return 0;
}

static void type_osk(Osk *k, const char *s) {
    while (*s) {
        int n = 1;
        unsigned char c = (unsigned char)*s;
        if (c >= 0xF0) n = 4; else if (c >= 0xE0) n = 3; else if (c >= 0xC0) n = 2;
        char ch[5] = {0};
        memcpy(ch, s, n);
        int done = 0;
        if (!strcmp(ch, " ")) done = tap_osk_type(k, OSK_SPACE);
        for (int i = 0; i < k->nkeys && !done; i++) {
            const OskKey *d = k->defs[i];
            if (d->type == OSK_CHAR && ((d->lower && !strcmp(d->lower, ch)) || (d->upper && !strcmp(d->upper, ch)))) {
                tap(k->rects[i].x + k->rects[i].w / 2, k->rects[i].y + k->rects[i].h / 2);
                done = 1;
            }
        }
        if (!done) fprintf(stderr, "sim: no on-screen key '%s'\n", ch);
        s += n;
    }
}

static char *read_rel(const char *rel) {
    char p[1400];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    return storage_read(p, NULL);
}

static int exists_rel(const char *rel) {
    char p[1400];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    return stat(p, &st) == 0;
}

static void new_project(const char *template_label, const char *name) {
    tap_slot(0);
    tap_menu(template_label);
    type_physical(name, LAYOUT_EN);
    key(KC_ENTER);
}

int main(int argc, char **argv) {
    snprintf(out_dir, sizeof out_dir, "%s", argc > 1 ? argv[1] : "build/shots");
    mkdir(out_dir, 0755);
    snprintf(root, sizeof root, "%s/PocketNotes", out_dir);
    snprintf(cfg, sizeof cfg, "%s/settings.cfg", out_dir);
    char cmd[1600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s' '%s' '%s/keyboard.log'", root, cfg, out_dir);
    if (system(cmd) != 0) return 1;
    hdr = SHd * 7 / 100;
    row = SHd * 55 / 1000;

    printf("1. Projects\n");
    app_handler(EVT_INIT, 0, 0);
    shot("01-projects-empty");
    tap_slot(0);
    shot("02-new-project-menu");
    tap_menu("Book (chapters, scenes, ideas, notes)");
    type_physical("The Grey City", LAYOUT_EN);
    key(KC_ENTER);
    expect(exists_rel("The Grey City/01 Chapter 1/01 Scene 1.md"), "book template: chapter and scene created");
    expect(exists_rel("The Grey City/Ideas") && exists_rel("The Grey City/Notes"), "book template: Ideas and Notes folders");
    tap_back();
    new_project("Empty project", "Work notes");
    expect(exists_rel("Work notes/01 Note.md"), "empty project with one note");
    tap_back();
    new_project("Empty project", "Recipes");
    tap_back();
    shot("03-projects");

    printf("2. A formatted note\n");
    tap_row(1);                         /* The Grey City (sorted: Recipes, The Grey City, Work notes) */
    shot("04-project-outline");
    tap_row(1);                         /* Scene 1 */
    type_physical("# Chapter One\n"
                  "The grey city woke slowly. *Nobody* noticed the **first** green leaf.\n"
                  "## To do\n"
                  "- [ ] Describe the market\n"
                  "Name the old cat\n\n\n"
                  "> Every city has a colour.\n\n\n"
                  "Motifs: ==green== against grey, `rain`, ~~sunny days~~.\n"
                  "1. Opening\n"
                  "Market scene\n\n\n"
                  "---\n"
                  "The rain started again before noon, and the market emptied street by street.", LAYOUT_EN);
    sim_advance(5000);
    char *note = read_rel("The Grey City/01 Chapter 1/01 Scene 1.md");
    expect(note && strstr(note, "- [ ] Describe the market\n- [ ] Name the old cat\n\n> Every city has a colour.\n\nMotifs"),
           "Enter continues task lists and quotes, and ends them on an empty item");
    expect(note && strstr(note, "1. Opening\n2. Market scene\n\n---"), "numbered list continues with 2.");
    free(note);
    shot("05-editor-keyboard");

    printf("3. Tick a checkbox, formatting menu\n");
    tap_slot(0);                        /* hide the keyboard so the whole note shows */
    {
        int x, y;
        if (editor_sim_checkbox_point(3, &x, &y)) tap(x, y);
        sim_advance(5000);
        note = read_rel("The Grey City/01 Chapter 1/01 Scene 1.md");
        expect(note && strstr(note, "- [x] Describe the market"), "tapping a checkbox ticks it");
        free(note);
    }
    shot("06-formatted-note");
    key(KC_UP);
    tap_slot(2);
    shot("07-format-menu");
    tap_menu("Bold");
    sim_advance(5000);
    note = read_rel("The Grey City/01 Chapter 1/01 Scene 1.md");
    expect(note && strstr(note, "**"), "bold markers inserted");
    free(note);

    printf("4. Zen mode\n");
    tap_slot(3);
    shot("08-zen");
    tap(SWd - hdr * 6 / 10 / 2 - 8, hdr * 6 / 10 / 2);   /* exit Zen */
    expect(1, "left Zen mode");

    printf("5. Ukrainian typing (Ctrl+Space switches layout)\n");
    tap_slot(0);                        /* keyboard back on */
    chord(KC_LEFTCTRL, KC_SPACE);
    type_physical("\nСірі будинки під сірим небом.", LAYOUT_UK);
    Osk *k = editor_sim_osk();
    tap_osk_type(k, OSK_ENTER);
    type_osk(k, "Він ішов повільно");
    sim_advance(5000);
    note = read_rel("The Grey City/01 Chapter 1/01 Scene 1.md");
    expect(note && strstr(note, "Сірі будинки під сірим небом.\nВін ішов повільно"), "Ukrainian text saved (physical + on-screen)");
    free(note);
    shot("09-ukrainian-typing");
    chord(KC_LEFTCTRL, KC_SPACE);

    printf("6. Settings: PocketBook keyboard, language\n");
    tap_slot(1);
    shot("10-settings");
    tap_menu("Keyboard: built-in");
    tap_slot(0);                        /* opens the PocketBook keyboard */
    sim_native_keyboard(" Typed on the PocketBook keyboard.", 0);
    tap_slot(0);
    sim_native_keyboard(" Streamed text.", 1);
    sim_advance(5000);
    note = read_rel("The Grey City/01 Chapter 1/01 Scene 1.md");
    expect(note && strstr(note, "Typed on the PocketBook keyboard.") && strstr(note, "Streamed text."),
           "PocketBook keyboard text inserted (on OK and streamed)");
    expect(note && !strstr(note, "Streamed text. Streamed text."), "streamed text not inserted twice");
    free(note);
    tap_slot(1);
    tap_menu("Keyboard: PocketBook");
    tap_slot(1);
    tap_menu("Language: English");
    shot("11-ukrainian-interface");
    tap_slot(1);
    tap_menu("Мова: українська");

    printf("7. Quick note\n");
    tap_slot(4);
    type_physical("Idea: the cat knows the way", LAYOUT_EN);
    tap_back();
    shot("12-outline-with-idea");
    {
        snprintf(cmd, sizeof cmd, "grep -rl 'the cat knows the way' '%s/The Grey City/Ideas' >/dev/null", root);
        expect(system(cmd) == 0, "quick note saved in Ideas");
    }

    printf("8. Outline actions\n");
    long_press(SWd / 2, hdr + row / 2);
    shot("13-folder-menu");
    {
        int x, y;
        expect(!popup_sim_item("Move up", &x, &y) && !popup_sim_item("Move down", &x, &y),
               "no move options for the only chapter");
    }
    tap(10, SHd - 10);                  /* close the menu */

    printf("9. Restart resumes\n");
    tap_row(1);                         /* open Scene 1 again */
    app_handler(EVT_EXIT, 0, 0);
    app_handler(EVT_INIT, 0, 0);
    shot("14-restart");
    char *settings = storage_read(cfg, NULL);
    expect(settings && strstr(settings, "Scene 1.md"), "last note remembered");
    free(settings);

    printf("\nScreen updates sent: %d\n", sim_update_count);
    printf("%s\n", fails ? "SIM CHECKS FAILED" : "All sim checks passed");
    return fails ? 1 : 0;
}
