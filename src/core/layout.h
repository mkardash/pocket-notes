/* Greedy word wrapping of one paragraph into lines that fit a pixel width. */
#ifndef LAYOUT_H
#define LAYOUT_H

/* Returns the pixel width of s[0..len). */
typedef int (*measure_fn)(void *ctx, const char *s, int len);

typedef struct {
    int start;  /* byte offset of the line inside the paragraph */
    int len;    /* bytes in the line, including trailing spaces */
} LineSpan;

/* Wraps s[0..len) into lines no wider than width (trailing spaces may overhang).
 * Writes spans into *lines (grown with realloc, capacity in *cap).
 * Returns the number of lines; an empty paragraph yields one empty line. */
int layout_wrap(const char *s, int len, int width, measure_fn measure, void *ctx,
                LineSpan **lines, int *cap);
/* Same, but the first line is only first_width wide (for a first-line indent). */
int layout_wrap_first(const char *s, int len, int first_width, int width, measure_fn measure, void *ctx,
                      LineSpan **lines, int *cap);

/* Byte offset within s[0..len) whose left edge is closest to pixel x. */
int layout_offset_at_x(const char *s, int len, int x, measure_fn measure, void *ctx);

#endif
