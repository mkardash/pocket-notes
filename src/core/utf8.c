#include "utf8.h"

static int is_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

int utf8_decode(const char *str, int len, uint32_t *cp) {
    const unsigned char *s = (const unsigned char *)str;
    if (len <= 0) { *cp = 0; return 0; }
    unsigned char c = s[0];
    if (c < 0x80) { *cp = c; return 1; }
    int n;
    uint32_t v;
    if ((c & 0xE0) == 0xC0) { n = 2; v = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { n = 3; v = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { n = 4; v = c & 0x07; }
    else { *cp = 0xFFFD; return 1; }
    if (n > len) { *cp = 0xFFFD; return 1; }
    for (int i = 1; i < n; i++) {
        if (!is_cont(s[i])) { *cp = 0xFFFD; return 1; }
        v = (v << 6) | (s[i] & 0x3F);
    }
    *cp = v;
    return n;
}

int utf8_encode(uint32_t cp, char *out) {
    unsigned char *o = (unsigned char *)out;
    if (cp < 0x80) { o[0] = (unsigned char)cp; return 1; }
    if (cp < 0x800) { o[0] = 0xC0 | (cp >> 6); o[1] = 0x80 | (cp & 0x3F); return 2; }
    if (cp < 0x10000) {
        o[0] = 0xE0 | (cp >> 12); o[1] = 0x80 | ((cp >> 6) & 0x3F); o[2] = 0x80 | (cp & 0x3F);
        return 3;
    }
    o[0] = 0xF0 | (cp >> 18); o[1] = 0x80 | ((cp >> 12) & 0x3F);
    o[2] = 0x80 | ((cp >> 6) & 0x3F); o[3] = 0x80 | (cp & 0x3F);
    return 4;
}

int utf8_prev(const char *str, int pos) {
    const unsigned char *s = (const unsigned char *)str;
    if (pos <= 0) return 0;
    int p = pos - 1;
    int back = 0;
    while (p > 0 && is_cont(s[p]) && back < 3) { p--; back++; }
    /* Make sure the lead byte really covers pos; otherwise step a single byte. */
    uint32_t cp;
    if (p + utf8_decode(str + p, pos - p, &cp) != pos) return pos - 1;
    return p;
}

int utf8_next(const char *s, int len, int pos) {
    if (pos >= len) return len;
    uint32_t cp;
    return pos + utf8_decode(s + pos, len - pos, &cp);
}

int utf8_count(const char *s, int len) {
    int n = 0, p = 0;
    while (p < len) { p = utf8_next(s, len, p); n++; }
    return n;
}

uint32_t utf8_tolower(uint32_t c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    if (c >= 0x410 && c <= 0x42F) return c + 32;      /* А..Я */
    if (c >= 0x400 && c <= 0x40F) return c + 80;      /* Ѐ..Џ incl. Є І Ї */
    if (c == 0x490) return 0x491;                     /* Ґ */
    return c;
}
