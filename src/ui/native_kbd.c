#include "native_kbd.h"
#include "app.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static NativeKbdOps ops;
static char buffer[4096];
static int streamed = 0;
static int open_now = 0;

static void log_line(const char *fmt, ...) {
    FILE *f = fopen(app_keyboard_log_path(), "a");
    if (!f) return;
    time_t now = time(NULL);
    char ts[32];
    strftime(ts, sizeof ts, "%H:%M:%S", localtime(&now));
    fprintf(f, "%s ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void on_change(void *ctx, const char *commit, const char *before, int replace_from, int replace_length) {
    log_line("change commit='%s' before='%s' replace_from=%d replace_length=%d",
             commit ? commit : "(null)", before ? before : "(null)", replace_from, replace_length);
    streamed = 1;
    /* Best guess until the log shows otherwise: replace_length characters before the cursor
     * are replaced by the committed text (how word suggestions usually work). */
    if (replace_length > 0 && ops.erase) ops.erase(replace_length);
    if (commit && *commit && ops.insert) ops.insert(commit);
}

static void on_close(char *text) {
    log_line("closed text='%s' streamed=%d", text ? text : "(null)", streamed);
    setKeyboardTextChangeCallback(NULL, NULL);
    open_now = 0;
    if (!streamed && text && *text && ops.insert) ops.insert(text);
    if (ops.closed) ops.closed();
}

void native_kbd_open(const char *title, NativeKbdOps o) {
    ops = o;
    streamed = 0;
    buffer[0] = 0;
    open_now = 1;
    log_line("open title='%s'", title ? title : "");
    setKeyboardTextChangeCallback(on_change, NULL);
    OpenKeyboard(title, buffer, sizeof buffer - 1, KBD_NORMAL, on_close);
}

int native_kbd_is_open(void) { return open_now; }
