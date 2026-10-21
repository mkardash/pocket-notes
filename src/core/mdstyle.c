#include "mdstyle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int is_space(char c) { return c == ' ' || c == '\t'; }
static int is_word_char(unsigned char c) {
    return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z') || c >= 0x80;
}
int md_is_escapable(char c) { return c && strchr("\\`*_{}[]()#+-.!|~=<>\"'", c) != NULL; }

int md_indent(const char *s, int len) {
    int i = 0;
    while (i < len && s[i] == ' ') i++;
    return i;
}

int md_is_fence(const char *s, int len) {
    int i = md_indent(s, len);
    if (i > 3 || len - i < 3) return 0;
    return (s[i] == '`' && s[i + 1] == '`' && s[i + 2] == '`') || (s[i] == '~' && s[i + 1] == '~' && s[i + 2] == '~');
}

static void detect_block(const char *s, int len, ParaStyle *o) {
    o->type = BLK_NORMAL;
    o->level = 0;
    o->marker_len = 0;
    o->bullet_off = -1;
    o->box_off = -1;
    o->number = 0;
    if (len == 0) { o->type = BLK_EMPTY; return; }
    int ind = md_indent(s, len);
    const char *p = s + ind;
    int rest = len - ind;
    if (rest == 0) { o->type = BLK_EMPTY; return; }

    /* Headings: 1-6 '#' then a space (or end of line). */
    if (ind <= 3 && p[0] == '#') {
        int h = 0;
        while (h < rest && p[h] == '#') h++;
        if (h <= 6 && (h == rest || p[h] == ' ')) {
            o->type = BLK_HEADING;
            o->level = h;
            o->marker_len = ind + h + (h < rest ? 1 : 0);
            return;
        }
    }
    /* Quotes: one or more '>' (optionally separated by spaces). */
    if (ind <= 3 && p[0] == '>') {
        int i = 0, depth = 0;
        while (i < rest && (p[i] == '>' || p[i] == ' ')) { if (p[i] == '>') depth++; i++; }
        o->type = BLK_QUOTE;
        o->level = depth;
        o->marker_len = ind + i;
        return;
    }
    /* Divider: 3+ of the same - * _ and nothing else but spaces. */
    if (ind <= 3 && (p[0] == '-' || p[0] == '*' || p[0] == '_')) {
        int count = 0, ok = 1;
        for (int i = 0; i < rest; i++) {
            if (p[i] == p[0]) count++;
            else if (p[i] != ' ') { ok = 0; break; }
        }
        if (ok && count >= 3) { o->type = BLK_HR; o->marker_len = len; return; }
    }
    /* Tables: rows start with '|'. A rule row has only | - : and spaces. */
    if (p[0] == '|') {
        int rule = 1;
        for (int i = 0; i < rest; i++) if (!strchr("|-: ", p[i])) { rule = 0; break; }
        o->type = rule && strchr(p, '-') ? BLK_TABLE_RULE : BLK_TABLE;
        return;
    }
    /* Bullet lists and task lists. */
    if ((p[0] == '-' || p[0] == '*' || p[0] == '+') && (rest == 1 || p[1] == ' ')) {
        o->level = ind / 2;
        o->bullet_off = ind;
        if (rest >= 5 && p[2] == '[' && p[4] == ']' && (rest == 5 || p[5] == ' ') &&
            (p[3] == ' ' || p[3] == 'x' || p[3] == 'X')) {
            o->type = p[3] == ' ' ? BLK_TODO : BLK_DONE;
            o->box_off = ind + 2;
            o->marker_len = ind + (rest > 5 ? 6 : 5);
            return;
        }
        o->type = BLK_BULLET;
        o->marker_len = ind + (rest > 1 ? 2 : 1);
        return;
    }
    /* Numbered lists: 1-9 digits then '.' or ')' then a space (or end). */
    int d = 0, n = 0;
    while (d < rest && d < 9 && p[d] >= '0' && p[d] <= '9') { n = n * 10 + (p[d] - '0'); d++; }
    if (d > 0 && d < rest && (p[d] == '.' || p[d] == ')') && (d + 1 == rest || p[d + 1] == ' ')) {
        o->type = BLK_NUMBER;
        o->level = ind / 2;
        o->number = n;
        o->bullet_off = ind;
        o->marker_len = ind + d + (d + 1 < rest ? 2 : 1);
        return;
    }
}

/* Closing delimiter for an opener at `open`; opener must be followed by non-space,
 * closer preceded by non-space. Returns -1 if none. */
static int find_close(const char *s, int len, const unsigned char *st, int open, const char *d, int dlen) {
    int inner = open + dlen;
    if (inner >= len || is_space(s[inner])) return -1;
    for (int i = inner + 1; i + dlen <= len; i++) {
        if (st[i] & (ST_CODE | ST_LITERAL)) continue;
        if (memcmp(s + i, d, dlen) != 0 || is_space(s[i - 1])) continue;
        if (dlen == 1 && i + 1 < len && s[i + 1] == d[0]) { i++; continue; }  /* half of a pair */
        return i;
    }
    return -1;
}

static void apply_pairs(const char *s, int len, unsigned char *st, int from, const char *d, int dlen,
                        int bits, int word_bound) {
    for (int i = from; i + dlen <= len; i++) {
        if (st[i] & (ST_CODE | ST_MARK | ST_LITERAL)) continue;
        if (memcmp(s + i, d, dlen) != 0) continue;
        if (dlen == 1 && i + 1 < len && s[i + 1] == d[0]) { i++; continue; }
        if (word_bound && i > 0 && is_word_char((unsigned char)s[i - 1])) continue;
        int close = find_close(s, len, st, i, d, dlen);
        if (close < 0) continue;
        if (word_bound && close + dlen < len && is_word_char((unsigned char)s[close + dlen])) continue;
        for (int k = 0; k < dlen; k++) { st[i + k] |= ST_MARK; st[close + k] |= ST_MARK; }
        for (int k = i + dlen; k < close; k++) st[k] |= bits;
        i = close + dlen - 1;
    }
}

/* [text](url), ![alt](src), <http://...>, [^footnote] */
static void apply_links(const char *s, int len, unsigned char *st, int from) {
    for (int i = from; i < len; i++) {
        if (st[i] & (ST_CODE | ST_LITERAL | ST_MARK)) continue;
        if (s[i] == '<') {
            int j = i + 1;
            while (j < len && s[j] != '>' && s[j] != ' ') j++;
            if (j < len && s[j] == '>' && (memchr(s + i, ':', j - i) || memchr(s + i, '@', j - i))) {
                st[i] |= ST_MARK;
                st[j] |= ST_MARK;
                for (int k = i + 1; k < j; k++) st[k] |= ST_LINK;
                i = j;
            }
            continue;
        }
        if (s[i] != '[') continue;
        int image = i > from && s[i - 1] == '!' && !(st[i - 1] & ST_LITERAL);
        int j = i + 1, depth = 1;
        while (j < len && depth > 0) {
            if (!(st[j] & ST_LITERAL)) { if (s[j] == '[') depth++; else if (s[j] == ']') depth--; }
            if (depth > 0) j++;
        }
        if (j >= len) continue;
        if (j > i + 1 && s[i + 1] == '^') {          /* footnote reference */
            for (int k = i; k <= j; k++) st[k] |= ST_MARK;
            i = j;
            continue;
        }
        if (j + 1 < len && s[j + 1] == '(') {
            int e = j + 2;
            while (e < len && s[e] != ')') e++;
            if (e >= len) continue;
            if (image) st[i - 1] |= ST_MARK;
            st[i] |= ST_MARK;
            for (int k = i + 1; k < j; k++) st[k] |= image ? ST_ITALIC : ST_LINK;
            for (int k = j; k <= e; k++) st[k] |= ST_MARK;
            i = e;
        }
    }
}

void md_parse(const char *s, int len, int in_code, ParaStyle *out) {
    if (out->cap < len + 1) {
        out->cap = len + 64;
        out->style = realloc(out->style, out->cap);
    }
    memset(out->style, 0, len + 1);
    unsigned char *st = out->style;

    if (md_is_fence(s, len)) {
        detect_block("", 0, out);
        out->type = BLK_FENCE;
        memset(st, ST_MARK, len);
        return;
    }
    if (in_code) {
        detect_block("", 0, out);
        out->type = BLK_CODE;
        memset(st, ST_CODE, len);
        return;
    }
    detect_block(s, len, out);
    for (int i = 0; i < out->marker_len; i++) st[i] = ST_MARK;
    if (out->type == BLK_HR) return;
    if (out->type == BLK_TABLE_RULE) { memset(st, ST_MARK, len); return; }
    int from = out->marker_len;

    /* Escapes: \* etc. — the backslash is markup, the next character is literal. */
    for (int i = from; i + 1 < len; i++) {
        if (s[i] == '\\' && md_is_escapable(s[i + 1])) { st[i] |= ST_MARK; st[i + 1] |= ST_LITERAL; i++; }
    }
    /* `code` spans: nothing inside is formatted. */
    for (int i = from; i < len; i++) {
        if (s[i] != '`' || (st[i] & ST_LITERAL)) continue;
        int j = i + 1;
        while (j < len && s[j] != '`') j++;
        if (j >= len) break;
        st[i] |= ST_MARK;
        st[j] |= ST_MARK;
        for (int k = i + 1; k < j; k++) st[k] |= ST_CODE;
        i = j;
    }
    if (out->type == BLK_TABLE)
        for (int i = 0; i < len; i++) if (s[i] == '|' && !(st[i] & (ST_CODE | ST_LITERAL))) st[i] |= ST_MARK;
    apply_links(s, len, st, from);
    apply_pairs(s, len, st, from, "***", 3, ST_BOLD | ST_ITALIC, 0);
    apply_pairs(s, len, st, from, "**", 2, ST_BOLD, 0);
    apply_pairs(s, len, st, from, "__", 2, ST_BOLD, 1);
    apply_pairs(s, len, st, from, "~~", 2, ST_STRIKE, 0);
    apply_pairs(s, len, st, from, "==", 2, ST_HL, 0);
    apply_pairs(s, len, st, from, "*", 1, ST_ITALIC, 0);
    apply_pairs(s, len, st, from, "_", 1, ST_ITALIC, 1);
}

void md_free(ParaStyle *ps) {
    free(ps->style);
    ps->style = NULL;
    ps->cap = 0;
}

void md_prefix_for(BlockFormat f, int indent, int number, char *out, int cap) {
    char sp[32];
    if (indent > 30) indent = 30;
    memset(sp, ' ', indent);
    sp[indent] = 0;
    switch (f) {
    case FMT_H1: snprintf(out, cap, "# "); break;
    case FMT_H2: snprintf(out, cap, "## "); break;
    case FMT_H3: snprintf(out, cap, "### "); break;
    case FMT_QUOTE: snprintf(out, cap, "> "); break;
    case FMT_BULLET: snprintf(out, cap, "%s- ", sp); break;
    case FMT_NUMBER: snprintf(out, cap, "%s%d. ", sp, number > 0 ? number : 1); break;
    case FMT_TODO: snprintf(out, cap, "%s- [ ] ", sp); break;
    default: out[0] = 0; break;
    }
}
