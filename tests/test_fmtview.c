#include "test.h"
#include "../src/core/mdstyle.h"
#include "../src/core/fmtview.h"

static ParaStyle ps;
static unsigned char hid[256];

/* Parses s into the shared ParaStyle and hidden mask. */
static const ParaStyle *parse(const char *s) {
    int len = (int)strlen(s);
    md_free(&ps);
    md_parse(s, len, 0, &ps);
    fv_hidden(s, len, &ps, hid);
    return &ps;
}

/* One char per byte: 'h' hidden, '.' visible. */
static const char *hidmask(const char *s) {
    static char out[256];
    int len = (int)strlen(s);
    parse(s);
    for (int i = 0; i < len && i < 255; i++) out[i] = hid[i] ? 'h' : '.';
    out[len < 255 ? len : 255] = 0;
    return out;
}

static int snap(const char *s, int off) {
    parse(s);
    return fv_snap(s, (int)strlen(s), &ps, hid, off);
}

static int prev(const char *s, int off) {
    parse(s);
    return fv_prev(s, (int)strlen(s), &ps, hid, off);
}

/* Canonical cursor offsets from the start, stepping with fv_next, as a comma list. */
static const char *stops(const char *s) {
    static char out[256];
    int len = (int)strlen(s), n = 0;
    parse(s);
    out[0] = 0;
    for (int off = fv_snap(s, len, &ps, hid, 0); off >= 0 && n < 200;
         off = fv_next(s, len, &ps, hid, off)) {
        n += snprintf(out + n, sizeof out - n, "%s%d", n ? "," : "", off);
    }
    return out;
}

/* Text with a '^' cursor marker; returns the inline style left of the cursor. */
static int style_left(const char *marked) {
    char buf[256];
    int len = 0, off = 0;
    for (const char *p = marked; *p; p++) {
        if (*p == '^') off = len;
        else buf[len++] = *p;
    }
    buf[len] = 0;
    parse(buf);
    return fv_style_left(buf, len, &ps, hid, off);
}

static void test_fmtview(void) {
    CHECK_STR(hidmask("# Title"), "hh.....");
    CHECK_STR(hidmask("**b** x"), "hh.hh..");
    CHECK_STR(hidmask("- item"), "......");
    CHECK_STR(hidmask("1. one"), "......");
    CHECK_STR(hidmask("- [ ] t"), ".......");
    CHECK_STR(hidmask("[t](u)"), "h.hhhh");
    CHECK_STR(hidmask("a\\*b"), ".h..");
    CHECK_STR(hidmask("| a | b |"), ".........");
    CHECK_STR(hidmask("---"), "hhh");
    CHECK_STR(hidmask("`c`"), "h.h");
    CHECK_STR(hidmask("**Ж**"), "hh..hh");
    CHECK_INT(fv_para_hidden(parse("---")), 1);  CHECK_INT(fv_para_hidden(parse("```c")), 1);
    CHECK_INT(fv_para_hidden(parse("|---|")), 1); CHECK_INT(fv_para_hidden(parse("# T")), 0);
    CHECK_INT(fv_content_start(parse("- [ ] t")), 6); CHECK_INT(fv_content_start(parse("> > q")), 4);
    CHECK_INT(snap("**bold**", 8), 6);  CHECK_INT(snap("# T", 0), 2);  CHECK_INT(snap("ab**c**", 4), 2);
    CHECK_INT(snap("**Жx**", 3), 0);                 /* mid-character, then leftmost */
    CHECK_STR(stops("ab**c**"), "0,1,2,5");
    CHECK_STR(stops("# **Ж**"), "2,6");
    CHECK_STR(stops("***x*** y"), "0,4,8,9");
    CHECK_STR(stops("[**b**](u) c"), "0,4,11,12");
    CHECK_INT(prev("# **Ж**", 6), 2);  CHECK_INT(prev("# **Ж**", 2), -1);
    CHECK_INT(style_left("**bo^ld**"), ST_BOLD);     /* '^' marks off */
    CHECK_INT(style_left("[t](u)^"), 0);             /* links excluded */
    CHECK_INT(style_left("# ^T"), 0);
}

void test_fmtview_all(void) {
    RUN(test_fmtview);
}
