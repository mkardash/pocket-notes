/* Formatted view editing: turns an edit made on the visible text into a change of the Markdown
 * source that renders exactly as the user intends, with no markup symbol appearing.
 * The paragraph text stays the source of truth. Each edit builds candidate texts in a fixed
 * order, parses each one and keeps the first whose rendering passes that tier's check. */
#ifndef FMTEDIT_H
#define FMTEDIT_H

/* Result of an edit. Start zeroed; the buffers are reused across calls (fe_free releases them).
 * s[0..len) is the new paragraph (NUL-terminated); s2 holds a second paragraph for edits that
 * make two (len2 is 0 otherwise); cursor is the canonical cursor offset in s. */
typedef struct { char *s; int len, cap; char *s2; int len2, cap2; int cursor; } FeOut;

/* Releases the buffers of o and zeroes it. */
void fe_free(FeOut *o);

/* md_parse calls made by fmtedit; tests reset and read it. */
extern int fe_parse_count;

/* Typing `t[0..tlen)` (no '\n') at canonical `off` with typing style `want` (FV_INLINE bits).
   Writes the new paragraph to o->s/len and the canonical cursor to o->cursor. Always succeeds.
   s must not point into o's buffers. */
void fe_insert(const char *s, int len, int in_code, int off, const char *t, int tlen, int want, FeOut *o);

#endif
