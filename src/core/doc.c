#include "doc.h"
#include "utf8.h"
#include <stdlib.h>
#include <string.h>

static void para_reserve(Para *p, int need) {
    if (need + 1 <= p->cap) return;
    int cap = p->cap ? p->cap : 32;
    while (cap < need + 1) cap *= 2;
    p->s = realloc(p->s, cap);
    p->cap = cap;
}

static void para_set(Para *p, const char *s, int len) {
    para_reserve(p, len);
    if (len) memcpy(p->s, s, len);
    p->len = len;
    p->s[len] = 0;
}

static void touch(Doc *d, Para *p) {
    p->gen = ++d->next_gen;
    d->modified = 1;
}

static void doc_reserve(Doc *d, int need) {
    if (need <= d->cap) return;
    int cap = d->cap ? d->cap : 16;
    while (cap < need) cap *= 2;
    d->p = realloc(d->p, sizeof(Para) * cap);
    memset(d->p + d->cap, 0, sizeof(Para) * (cap - d->cap));
    d->cap = cap;
}

/* Inserts an empty paragraph at index i. */
static Para *insert_para(Doc *d, int i) {
    doc_reserve(d, d->n + 1);
    memmove(d->p + i + 1, d->p + i, sizeof(Para) * (d->n - i));
    memset(&d->p[i], 0, sizeof(Para));
    d->n++;
    para_set(&d->p[i], "", 0);
    d->p[i].gen = ++d->next_gen;
    return &d->p[i];
}

static void remove_para(Doc *d, int i) {
    free(d->p[i].s);
    memmove(d->p + i, d->p + i + 1, sizeof(Para) * (d->n - i - 1));
    d->n--;
    memset(&d->p[d->n], 0, sizeof(Para));
}

void doc_set_para(Doc *d, int i, const char *s, int len) {
    if (i < 0 || i >= d->n) return;
    para_set(&d->p[i], s, len);
    touch(d, &d->p[i]);
    if (d->cp == i && d->co > len) d->co = len;
}

void doc_insert_para(Doc *d, int i, const char *s, int len) {
    if (i < 0) i = 0;
    if (i > d->n) i = d->n;
    para_set(insert_para(d, i), s, len);
    d->modified = 1;
    if (i <= d->cp) d->cp++;
}

void doc_delete_para(Doc *d, int i) {
    if (d->n <= 1 || i < 0 || i >= d->n) return;
    remove_para(d, i);
    d->modified = 1;
    if (i < d->cp) d->cp--;
    else if (i == d->cp) {
        if (i > 0) { d->cp = i - 1; d->co = d->p[d->cp].len; }
        else d->co = 0;
    }
}

Doc *doc_from_text(const char *text, int len) {
    Doc *d = calloc(1, sizeof(Doc));
    if (len >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0) { text += 3; len -= 3; }
    int start = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || text[i] == '\n' || text[i] == '\r') {
            Para *p = insert_para(d, d->n);
            para_set(p, text + start, i - start);
            if (i < len && text[i] == '\r' && i + 1 < len && text[i + 1] == '\n') i++;
            start = i + 1;
        }
    }
    d->modified = 0;
    return d;
}

void doc_free(Doc *d) {
    if (!d) return;
    for (int i = 0; i < d->n; i++) free(d->p[i].s);
    free(d->p);
    free(d);
}

char *doc_text(const Doc *d, int *len_out) {
    int total = 0;
    for (int i = 0; i < d->n; i++) total += d->p[i].len + 1;
    char *out = malloc(total + 1);
    int pos = 0;
    for (int i = 0; i < d->n; i++) {
        memcpy(out + pos, d->p[i].s, d->p[i].len);
        pos += d->p[i].len;
        if (i + 1 < d->n) out[pos++] = '\n';
    }
    out[pos] = 0;
    if (len_out) *len_out = pos;
    return out;
}

static void insert_plain(Doc *d, const char *s, int len) {
    if (len <= 0) return;
    Para *p = &d->p[d->cp];
    para_reserve(p, p->len + len);
    memmove(p->s + d->co + len, p->s + d->co, p->len - d->co + 1);
    memcpy(p->s + d->co, s, len);
    p->len += len;
    d->co += len;
    touch(d, p);
}

static void split_at_cursor(Doc *d) {
    Para *np = insert_para(d, d->cp + 1);
    Para *p = &d->p[d->cp]; /* re-fetch: insert_para may move the array */
    para_set(np, p->s + d->co, p->len - d->co);
    p->len = d->co;
    p->s[p->len] = 0;
    touch(d, p);
    touch(d, np);
    d->cp++;
    d->co = 0;
}

void doc_insert(Doc *d, const char *s, int len) {
    int start = 0;
    for (int i = 0; i <= len; i++) {
        if (i == len || s[i] == '\n') {
            insert_plain(d, s + start, i - start);
            if (i < len) split_at_cursor(d);
            start = i + 1;
        }
    }
}

static void join_with_next(Doc *d, int i) {
    Para *a = &d->p[i], *b = &d->p[i + 1];
    para_reserve(a, a->len + b->len);
    memcpy(a->s + a->len, b->s, b->len + 1);
    a->len += b->len;
    touch(d, a);
    remove_para(d, i + 1);
}

void doc_backspace(Doc *d) {
    if (d->co > 0) {
        Para *p = &d->p[d->cp];
        int prev = utf8_prev(p->s, d->co);
        memmove(p->s + prev, p->s + d->co, p->len - d->co + 1);
        p->len -= d->co - prev;
        d->co = prev;
        touch(d, p);
    } else if (d->cp > 0) {
        int off = d->p[d->cp - 1].len;
        join_with_next(d, d->cp - 1);
        d->cp--;
        d->co = off;
    }
}

void doc_delete_forward(Doc *d) {
    Para *p = &d->p[d->cp];
    if (d->co < p->len) {
        int next = utf8_next(p->s, p->len, d->co);
        memmove(p->s + d->co, p->s + next, p->len - next + 1);
        p->len -= next - d->co;
        touch(d, p);
    } else if (d->cp + 1 < d->n) {
        join_with_next(d, d->cp);
    }
}

void doc_left(Doc *d) {
    if (d->co > 0) d->co = utf8_prev(d->p[d->cp].s, d->co);
    else if (d->cp > 0) { d->cp--; d->co = d->p[d->cp].len; }
}

void doc_right(Doc *d) {
    Para *p = &d->p[d->cp];
    if (d->co < p->len) d->co = utf8_next(p->s, p->len, d->co);
    else if (d->cp + 1 < d->n) { d->cp++; d->co = 0; }
}

void doc_set_cursor(Doc *d, int para, int off) {
    if (para < 0) para = 0;
    if (para >= d->n) para = d->n - 1;
    Para *p = &d->p[para];
    if (off < 0) off = 0;
    if (off > p->len) off = p->len;
    /* Snap to a character boundary. */
    while (off > 0 && off < p->len && (((unsigned char)p->s[off]) & 0xC0) == 0x80) off--;
    d->cp = para;
    d->co = off;
}

/* A word is a run of non-space characters containing at least one letter or digit. */
static int is_wordish(uint32_t c) {
    if (c < 0x80) return (c >= '0' && c <= '9') || ((c | 32) >= 'a' && (c | 32) <= 'z');
    if (c >= 0x2000 && c <= 0x206F) return 0; /* general punctuation: dashes, quotes, ellipsis */
    if (c == 0xAB || c == 0xBB || c == 0xA0) return 0;
    return 1;
}

int doc_word_count(const Doc *d) {
    int words = 0;
    for (int i = 0; i < d->n; i++) {
        const Para *p = &d->p[i];
        int pos = 0, in_token = 0, token_has_word = 0;
        while (pos <= p->len) {
            uint32_t c = 0;
            int n = pos < p->len ? utf8_decode(p->s + pos, p->len - pos, &c) : 1;
            int space = pos == p->len || c == ' ' || c == '\t' || c == 0xA0;
            if (space) {
                if (in_token && token_has_word) words++;
                in_token = token_has_word = 0;
            } else {
                in_token = 1;
                if (is_wordish(c)) token_has_word = 1;
            }
            pos += n;
        }
    }
    return words;
}
