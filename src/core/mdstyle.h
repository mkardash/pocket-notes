/* Markdown styling for one paragraph (one line of the file): block type plus inline styles.
 * The text is never changed; markup characters are tagged so they can be drawn dimmed.
 * Covers CommonMark/GFM basics: headings, emphasis, strikethrough, ==highlight==, code,
 * fenced code blocks, quotes, nested lists, task lists, links, images, tables, dividers, escapes. */
#ifndef MDSTYLE_H
#define MDSTYLE_H

typedef enum {
    BLK_NORMAL, BLK_EMPTY, BLK_HEADING, BLK_QUOTE, BLK_BULLET, BLK_NUMBER, BLK_TODO, BLK_DONE,
    BLK_HR, BLK_FENCE, BLK_CODE, BLK_TABLE, BLK_TABLE_RULE
} BlockType;

/* Inline style bits, one byte per byte of text. */
enum {
    ST_BOLD = 1, ST_ITALIC = 2, ST_STRIKE = 4, ST_CODE = 8,
    ST_MARK = 16,       /* markup characters: drawn dimmed */
    ST_HL = 32,         /* ==highlight== */
    ST_LINK = 64,       /* link text / URL: underlined */
    ST_LITERAL = 128    /* escaped character: never starts or ends formatting */
};

typedef struct {
    BlockType type;
    int level;          /* heading 1-6; quote depth; list nesting (0 = top) */
    int marker_len;     /* bytes of the block prefix incl. leading spaces, e.g. "  - [ ] " */
    int bullet_off;     /* byte offset of the list marker character (-1 if none) */
    int box_off;        /* TODO/DONE: byte offset of "[" (-1 if none) */
    int number;         /* BLK_NUMBER: the number */
    unsigned char *style;
    int cap;
} ParaStyle;

/* Is this line a code fence (``` or ~~~)? */
int md_is_fence(const char *s, int len);
/* Fills `out` for s[0..len). in_code: the line is inside a fenced code block. */
void md_parse(const char *s, int len, int in_code, ParaStyle *out);
void md_free(ParaStyle *ps);

/* Is c a punctuation character that a backslash can escape? */
int md_is_escapable(char c);

/* Block prefixes used by the format menu. */
typedef enum { FMT_NORMAL, FMT_H1, FMT_H2, FMT_H3, FMT_BULLET, FMT_NUMBER, FMT_TODO, FMT_QUOTE } BlockFormat;
/* Prefix text for a block format, keeping the list nesting `indent` (in spaces). */
void md_prefix_for(BlockFormat f, int indent, int number, char *out, int cap);
/* Leading spaces of a line. */
int md_indent(const char *s, int len);

#endif
