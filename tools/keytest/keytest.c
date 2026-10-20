/* Key test: shows every input event InkView delivers, and lists /dev/input devices.
 * Used to find out whether a physical (Bluetooth/USB) keyboard reaches apps. */
#include <inkview.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define MAXLINES 40
static char lines[MAXLINES][160];
static int nlines = 0;
static ifont *font = NULL;
static FILE *logf = NULL;

static void add_line(const char *s) {
    if (nlines == MAXLINES) {
        memmove(lines[0], lines[1], sizeof(lines[0]) * (MAXLINES - 1));
        nlines--;
    }
    snprintf(lines[nlines++], sizeof(lines[0]), "%s", s);
    if (logf) { fprintf(logf, "%s\n", s); fflush(logf); }
}

static void redraw(int full) {
    ClearScreen();
    SetFont(font, BLACK);
    DrawString(20, 20, "PocketNotes key test - press keys on a keyboard. Tap top-right corner to exit.");
    DrawLine(0, 70, ScreenWidth(), 70, BLACK);
    for (int i = 0; i < nlines; i++) DrawString(20, 80 + i * 38, lines[i]);
    if (full) FullUpdate(); else PartialUpdate(0, 0, ScreenWidth(), ScreenHeight());
}

static void scan_input_devices(void) {
    DIR *d = opendir("/dev/input");
    if (!d) { add_line("cannot open /dev/input"); return; }
    struct dirent *e;
    char buf[160], path[64];
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        snprintf(buf, sizeof buf, "%s: %s", path, fd >= 0 ? "READABLE" : "no access");
        if (fd >= 0) close(fd);
        add_line(buf);
    }
    closedir(d);
    FILE *f = fopen("/proc/bus/input/devices", "r");
    if (f) {
        char l[256];
        while (fgets(l, sizeof l, f)) {
            if (strncmp(l, "N: Name=", 8) == 0) { l[strcspn(l, "\n")] = 0; add_line(l); }
        }
        fclose(f);
    }
}

static const char *evname(int t) {
    switch (t) {
    case EVT_KEYPRESS: return "KEYPRESS"; case EVT_KEYRELEASE: return "KEYRELEASE";
    case EVT_KEYREPEAT: return "KEYREPEAT"; case EVT_KEYPRESS_EXT: return "KEYPRESS_EXT";
    case EVT_KEYRELEASE_EXT: return "KEYRELEASE_EXT"; case EVT_KEYREPEAT_EXT: return "KEYREPEAT_EXT";
    case EVT_EXT_KB: return "EXT_KB"; case EVT_LETTER: return "LETTER";
    case EVT_TEXTCLEAR: return "TEXTCLEAR"; default: return NULL;
    }
}

static int handler(int type, int par1, int par2) {
    char buf[160];
    switch (type) {
    case EVT_INIT:
        logf = fopen("/mnt/ext1/writer-keytest.log", "w");
        font = OpenFont(DEFAULTFONT, 26, 1);
        add_line("Input devices:");
        scan_input_devices();
        add_line("---- events ----");
        redraw(1);
        return 1;
    case EVT_SHOW:
        redraw(1);
        return 1;
    case EVT_POINTERUP:
        if (par1 > ScreenWidth() - 200 && par2 < 200) { if (logf) fclose(logf); CloseApp(); }
        return 1;
    case EVT_POINTERDOWN: case EVT_POINTERMOVE: case EVT_POINTERDRAG:
    case EVT_POINTERLONG: case EVT_POINTERHOLD: case EVT_TOUCHDOWN: case EVT_TOUCHUP:
    case EVT_TOUCHMOVE: case EVT_MTSYNC: case EVT_REPAINT:
        return 0;
    case EVT_EXIT:
        if (logf) fclose(logf);
        return 1;
    default: {
        const char *n = evname(type);
        if (n) snprintf(buf, sizeof buf, "%s  code=%d (0x%x)  par2=%d", n, par1, par1, par2);
        else snprintf(buf, sizeof buf, "event %d  par1=%d  par2=%d", type, par1, par2);
        add_line(buf);
        redraw(0);
        /* Claim key events so the system doesn't act on them. */
        return ISKEYEVENT(type) || type == EVT_EXT_KB || type == EVT_LETTER;
    }
    }
}

int main(void) {
    InkViewMain(handler);
    return 0;
}
