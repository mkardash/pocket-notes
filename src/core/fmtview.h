/* Formatted view helpers: which bytes of a paragraph are hidden, and where the cursor may rest.
 * Every function takes the paragraph text s[0..len) and its ParaStyle from md_parse. */
#ifndef FMTVIEW_H
#define FMTVIEW_H
#include "mdstyle.h"

/* Inline styles that carry over when typing after a character. */
#define FV_INLINE (ST_BOLD | ST_ITALIC | ST_STRIKE | ST_CODE | ST_HL)

/* Is the whole paragraph hidden (divider, code fence, table rule)? */
int fv_para_hidden(const ParaStyle *ps);
/* Fills hid[0..len) with 1 for bytes not drawn in the formatted view, else 0. */
void fv_hidden(const char *s, int len, const ParaStyle *ps, unsigned char *hid);
/* First byte offset where the cursor may rest (after the block prefix). */
int fv_content_start(const ParaStyle *ps);
/* Moves off to the canonical cursor position: a character start, left of any hidden bytes. */
int fv_snap(const char *s, int len, const ParaStyle *ps, const unsigned char *hid, int off);
/* Cursor position after the next visible character at or after off; -1 at the end. */
int fv_next(const char *s, int len, const ParaStyle *ps, const unsigned char *hid, int off);
/* Cursor position before the last visible character before off; -1 at the start. */
int fv_prev(const char *s, int len, const ParaStyle *ps, const unsigned char *hid, int off);
/* Inline style (FV_INLINE bits) of the last visible character left of off, or 0. */
int fv_style_left(const char *s, int len, const ParaStyle *ps, const unsigned char *hid, int off);

#endif
