/* Formatted view helpers: hidden markup bytes and cursor positions. */
#include "fmtview.h"
#include "utf8.h"

int fv_para_hidden(const ParaStyle *ps) {
    return ps->type == BLK_HR || ps->type == BLK_FENCE || ps->type == BLK_TABLE_RULE;
}

void fv_hidden(const char *s, int len, const ParaStyle *ps, unsigned char *hid) {
    if (fv_para_hidden(ps)) {
        for (int i = 0; i < len; i++) hid[i] = 1;
        return;
    }
    for (int i = 0; i < len; i++) {
        if (ps->type == BLK_CODE) hid[i] = 0;
        else if (ps->type == BLK_TABLE && s[i] == '|') hid[i] = 0;
        else hid[i] = (ps->style[i] & ST_MARK) != 0;
    }
    /* Heading markers are always hidden; other block markers are drawn as bars, glyphs or boxes. */
    if (ps->type == BLK_HEADING)
        for (int i = 0; i < ps->marker_len && i < len; i++) hid[i] = 1;
    else if (ps->type >= BLK_QUOTE && ps->type <= BLK_DONE)
        for (int i = 0; i < ps->marker_len && i < len; i++) hid[i] = 0;
}

int fv_content_start(const ParaStyle *ps) {
    switch (ps->type) {
    case BLK_HEADING: case BLK_QUOTE: case BLK_BULLET: case BLK_NUMBER: case BLK_TODO: case BLK_DONE:
        return ps->marker_len;
    default:
        return 0;
    }
}

int fv_snap(const char *s, int len, const ParaStyle *ps, const unsigned char *hid, int off) {
    int start = fv_content_start(ps);
    if (off > len) off = len;
    if (off < start) off = start;
    while (off > start && off < len && ((unsigned char)s[off] & 0xC0) == 0x80) off--;
    while (off > start && hid[off - 1]) off--;
    return off;
}

int fv_next(const char *s, int len, const ParaStyle *ps, const unsigned char *hid, int off) {
    int v = off < fv_content_start(ps) ? fv_content_start(ps) : off;
    while (v < len && hid[v]) v++;
    if (v >= len) return -1;
    return fv_snap(s, len, ps, hid, utf8_next(s, len, v));
}

int fv_prev(const char *s, int len, const ParaStyle *ps, const unsigned char *hid, int off) {
    int start = fv_content_start(ps);
    int v = (off > len ? len : off) - 1;
    while (v >= start && hid[v]) v--;
    if (v < start) return -1;
    while (v > start && ((unsigned char)s[v] & 0xC0) == 0x80) v--;
    return fv_snap(s, len, ps, hid, v);
}

int fv_style_left(const char *s, int len, const ParaStyle *ps, const unsigned char *hid, int off) {
    int start = fv_content_start(ps);
    int v = (off > len ? len : off) - 1;
    while (v >= start && hid[v]) v--;
    return v < start ? 0 : ps->style[v] & FV_INLINE;
}
