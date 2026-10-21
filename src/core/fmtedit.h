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

/* Removes the visible character before / after canonical off. Returns 0 (o untouched) when there is none.
   Otherwise writes the new paragraph to o->s/len and the canonical cursor to o->cursor.
   Content starts after the block prefix, so the prefix is never deleted here. s must not point into o. */
int fe_delete_back(const char *s, int len, int in_code, int off, FeOut *o);
int fe_delete_forward(const char *s, int len, int in_code, int off, FeOut *o);

/* Enter at canonical off: left text → o->s, right text → o->s2 (no block prefix added).
   Styled runs that span off are closed on the left and reopened on the right. Inside link text
   the split moves to just after the link. o->cursor is 0 (the start of the right text).
   s must not point into o. */
void fe_split(const char *s, int len, int in_code, int off, FeOut *o);

/* Appends paragraph b's content (its block prefix dropped) to a. o->cursor = canonical join point.
   a and b must not point into o. */
void fe_join(const char *a, int alen, const char *b, int blen, FeOut *o);

/* Adds or removes one inline style bit on visible bytes [a, b) (a word). Returns 0 and leaves o
   untouched when no candidate renders correctly — never damages the text. The bit is removed when
   every visible byte of the word has it, else added. bit is one of ST_BOLD, ST_ITALIC, ST_STRIKE,
   ST_HL, ST_CODE. o->cursor is the canonical spot after the word. */
int fe_restyle(const char *s, int len, int in_code, int a, int b, int bit, FeOut *o);

#endif
