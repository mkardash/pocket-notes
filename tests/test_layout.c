#include "test.h"
#include "../src/core/layout.h"
#include "../src/core/utf8.h"
#include <stdlib.h>

/* Fake font: every character is 10px wide. */
static int mono(void *ctx, const char *s, int len) { (void)ctx; return utf8_count(s, len) * 10; }

static char lines_buf[512];
static const char *wrap(const char *s, int width) {
    LineSpan *lines = NULL;
    int cap = 0;
    int n = layout_wrap(s, (int)strlen(s), width, mono, NULL, &lines, &cap);
    lines_buf[0] = 0;
    for (int i = 0; i < n; i++) {
        strncat(lines_buf, "[", sizeof lines_buf - strlen(lines_buf) - 1);
        strncat(lines_buf, s + lines[i].start, lines[i].len);
        strncat(lines_buf, "]", sizeof lines_buf - strlen(lines_buf) - 1);
    }
    free(lines);
    return lines_buf;
}

static void test_wrap_words(void) {
    CHECK_STR(wrap("", 100), "[]");
    CHECK_STR(wrap("abc def", 100), "[abc def]");
    CHECK_STR(wrap("abc def ghi", 70), "[abc def ][ghi]");
    CHECK_STR(wrap("abc def ghi", 60), "[abc ][def ][ghi]");
    /* Cyrillic is measured by characters, not bytes. */
    CHECK_STR(wrap("сірі хмари", 40), "[сірі ][хмар][и]");
    CHECK_STR(wrap("сірі хмари", 50), "[сірі ][хмари]");
}

static void test_wrap_long_word_and_spaces(void) {
    CHECK_STR(wrap("abcdefghij", 40), "[abcd][efgh][ij]");
    CHECK_STR(wrap("ab abcdefghij", 40), "[ab ][abcd][efgh][ij]");
    CHECK_STR(wrap("  ab", 100), "[  ab]");
    CHECK_STR(wrap("ab   ", 30), "[ab   ]");
    CHECK_STR(wrap("   ", 30), "[   ]");
    CHECK_STR(wrap("ab  cd", 40), "[ab  ][cd]");
}

static const char *wrap_first(const char *s, int first, int width) {
    LineSpan *lines = NULL;
    int cap = 0;
    int n = layout_wrap_first(s, (int)strlen(s), first, width, mono, NULL, &lines, &cap);
    lines_buf[0] = 0;
    for (int i = 0; i < n; i++) {
        strncat(lines_buf, "[", sizeof lines_buf - strlen(lines_buf) - 1);
        strncat(lines_buf, s + lines[i].start, lines[i].len);
        strncat(lines_buf, "]", sizeof lines_buf - strlen(lines_buf) - 1);
    }
    free(lines);
    return lines_buf;
}

static void test_wrap_first_line_indent(void) {
    /* First line is 30px narrower, the rest use the full 70px. */
    CHECK_STR(wrap_first("abc def ghi jkl", 40, 70), "[abc ][def ghi ][jkl]");
    CHECK_STR(wrap_first("abcdefgh", 40, 70), "[abcd][efgh]");
    CHECK_STR(wrap_first("ab", 40, 70), "[ab]");
}

static void test_offset_at_x(void) {
    const char *s = "абв";
    CHECK_INT(layout_offset_at_x(s, 6, 0, mono, NULL), 0);
    CHECK_INT(layout_offset_at_x(s, 6, 4, mono, NULL), 0);
    CHECK_INT(layout_offset_at_x(s, 6, 6, mono, NULL), 2);
    CHECK_INT(layout_offset_at_x(s, 6, 16, mono, NULL), 4);
    CHECK_INT(layout_offset_at_x(s, 6, 500, mono, NULL), 6);
}

void test_layout_all(void) {
    RUN(test_wrap_words);
    RUN(test_wrap_long_word_and_spaces);
    RUN(test_offset_at_x);
    RUN(test_wrap_first_line_indent);
}
