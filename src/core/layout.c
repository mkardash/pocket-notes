#include "layout.h"
#include "utf8.h"
#include <stdlib.h>

static void push(LineSpan **lines, int *cap, int *n, int start, int len) {
    if (*n >= *cap) {
        *cap = *cap ? *cap * 2 : 8;
        *lines = realloc(*lines, sizeof(LineSpan) * (*cap));
    }
    (*lines)[*n].start = start;
    (*lines)[*n].len = len;
    (*n)++;
}

/* Longest prefix of s[start..end) (at least one character) that fits width. */
static int fit_chars(const char *s, int start, int end, int width, measure_fn m, void *ctx) {
    int pos = utf8_next(s, end, start);
    while (pos < end) {
        int next = utf8_next(s, end, pos);
        if (m(ctx, s + start, next - start) > width) break;
        pos = next;
    }
    return pos;
}

int layout_wrap(const char *s, int len, int width, measure_fn m, void *ctx,
                LineSpan **lines, int *cap) {
    return layout_wrap_first(s, len, width, width, m, ctx, lines, cap);
}

int layout_wrap_first(const char *s, int len, int first_width, int full_width, measure_fn m, void *ctx,
                      LineSpan **lines, int *cap) {
    int n = 0;
    if (full_width < 1) full_width = 1;
    if (first_width < 1) first_width = 1;
    int width = first_width;
    int line_start = 0, line_w = 0, has_word = 0;
    int pos = 0;
    while (pos < len) {
        /* Next token: a run of spaces, then a word. Spaces stay on the line they follow. */
        int sp_start = pos;
        while (pos < len && s[pos] == ' ') pos++;
        int word_start = pos;
        while (pos < len && s[pos] != ' ') pos++;
        int word_end = pos;
        if (word_end == word_start) break; /* only trailing spaces left */

        int sp_w = word_start > sp_start ? m(ctx, s + sp_start, word_start - sp_start) : 0;
        int word_w = m(ctx, s + word_start, word_end - word_start);

        if (has_word && line_w + sp_w + word_w > width) {
            push(lines, cap, &n, line_start, word_start - line_start);
            width = full_width;
            line_start = word_start;
            line_w = 0;
            sp_w = 0;
            has_word = 0;
        }
        if (line_w + sp_w + word_w <= width) {
            line_w += sp_w + word_w;
            has_word = 1;
            continue;
        }
        /* Too wide even for an empty line: break it by characters. */
        int seg = line_start;
        for (;;) {
            int cut = fit_chars(s, seg, word_end, width, m, ctx);
            if (cut >= word_end) {
                line_start = seg;
                line_w = m(ctx, s + seg, word_end - seg);
                has_word = 1;
                break;
            }
            push(lines, cap, &n, seg, cut - seg);
            width = full_width;
            seg = cut;
        }
    }
    push(lines, cap, &n, line_start, len - line_start);
    return n;
}

int layout_offset_at_x(const char *s, int len, int x, measure_fn m, void *ctx) {
    if (x <= 0) return 0;
    int pos = 0, prev_w = 0;
    while (pos < len) {
        int next = utf8_next(s, len, pos);
        int w = m(ctx, s, next);
        if (w >= x) return (x - prev_w < w - x) ? pos : next;
        prev_w = w;
        pos = next;
    }
    return len;
}
