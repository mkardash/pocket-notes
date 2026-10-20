#include "test.h"
#include "../src/core/doc.h"
#include "../src/core/utf8.h"
#include <stdlib.h>

static char *text_of(Doc *d) { return doc_text(d, NULL); }

static void test_utf8(void) {
    const char *s = "Привіт";           /* 6 Cyrillic letters, 2 bytes each */
    CHECK_INT(utf8_count(s, (int)strlen(s)), 6);
    CHECK_INT(utf8_next(s, 12, 0), 2);
    CHECK_INT(utf8_prev(s, 12), 10);
    CHECK_INT(utf8_prev(s, 2), 0);
    char buf[4];
    CHECK_INT(utf8_encode(0x0457, buf), 2);    /* ї */
    uint32_t cp;
    CHECK_INT(utf8_decode(buf, 2, &cp), 2);
    CHECK_INT(cp, 0x0457);
    CHECK_INT(utf8_tolower(0x0407), 0x0457);   /* Ї -> ї */
    CHECK_INT(utf8_tolower(0x0490), 0x0491);   /* Ґ -> ґ */
    CHECK_INT(utf8_tolower('Q'), 'q');
}

static void test_roundtrip(void) {
    const char *src = "Перший рядок\nДругий\n\nЧетвертий\n";
    Doc *d = doc_from_text(src, (int)strlen(src));
    CHECK_INT(d->n, 5);
    CHECK_INT(d->modified, 0);
    char *t = text_of(d);
    CHECK_STR(t, src);
    free(t);
    doc_free(d);

    d = doc_from_text("\xEF\xBB\xBF" "a\r\nb\rc", 9);
    CHECK_INT(d->n, 3);
    t = text_of(d);
    CHECK_STR(t, "a\nb\nc");
    free(t);
    doc_free(d);

    d = doc_from_text("", 0);
    CHECK_INT(d->n, 1);
    t = text_of(d);
    CHECK_STR(t, "");
    free(t);
    doc_free(d);
}

static void test_insert_and_split(void) {
    Doc *d = doc_from_text("", 0);
    doc_insert(d, "Ліс", (int)strlen("Ліс"));
    CHECK_INT(d->modified, 1);
    CHECK_INT(d->co, 6);
    doc_insert(d, "\n", 1);
    doc_insert(d, "Поле", (int)strlen("Поле"));
    char *t = text_of(d);
    CHECK_STR(t, "Ліс\nПоле");
    free(t);
    CHECK_INT(d->cp, 1);

    /* Split in the middle of a paragraph. */
    doc_set_cursor(d, 0, 2);        /* after "Л" */
    doc_insert(d, "\n", 1);
    t = text_of(d);
    CHECK_STR(t, "Л\nіс\nПоле");
    free(t);
    CHECK_INT(d->cp, 1);
    CHECK_INT(d->co, 0);

    /* Multi-line insert. */
    doc_set_cursor(d, 2, 999);
    doc_insert(d, " a\nb\nc", 6);
    t = text_of(d);
    CHECK_STR(t, "Л\nіс\nПоле a\nb\nc");
    free(t);
    CHECK_INT(d->cp, 4);
    CHECK_INT(d->co, 1);
    doc_free(d);
}

static void test_delete(void) {
    Doc *d = doc_from_text("Аб\nВг", 9);
    doc_set_cursor(d, 1, 0);
    doc_backspace(d);                 /* joins paragraphs */
    char *t = text_of(d);
    CHECK_STR(t, "АбВг");
    free(t);
    CHECK_INT(d->cp, 0);
    CHECK_INT(d->co, 4);
    doc_backspace(d);                 /* removes "б" (2 bytes) */
    t = text_of(d);
    CHECK_STR(t, "АВг");
    free(t);
    CHECK_INT(d->co, 2);
    doc_delete_forward(d);            /* removes "В" */
    t = text_of(d);
    CHECK_STR(t, "Аг");
    free(t);
    doc_set_cursor(d, 0, 0);
    doc_backspace(d);                 /* at document start: no-op */
    t = text_of(d);
    CHECK_STR(t, "Аг");
    free(t);
    doc_set_cursor(d, 0, 999);
    doc_delete_forward(d);            /* at document end: no-op */
    CHECK_INT(d->n, 1);
    doc_free(d);
}

static void test_cursor_moves(void) {
    Doc *d = doc_from_text("аб\nв", 7);
    doc_set_cursor(d, 0, 1);          /* inside "а": snaps back to 0 */
    CHECK_INT(d->co, 0);
    doc_right(d);
    CHECK_INT(d->co, 2);
    doc_right(d);
    doc_right(d);                     /* crosses into next paragraph */
    CHECK_INT(d->cp, 1);
    CHECK_INT(d->co, 0);
    doc_left(d);
    CHECK_INT(d->cp, 0);
    CHECK_INT(d->co, 4);
    doc_set_cursor(d, 9, 9);
    CHECK_INT(d->cp, 1);
    CHECK_INT(d->co, 2);
    doc_free(d);
}

static void test_word_count(void) {
    const char *s = "Тьмяні вогні — кольори, ось і все...\n\n«Сірий» сер'є 42";
    Doc *d = doc_from_text(s, (int)strlen(s));
    CHECK_INT(doc_word_count(d), 9);
    doc_free(d);
}

static void test_gen_changes(void) {
    Doc *d = doc_from_text("a\nb", 3);
    unsigned g0 = d->p[0].gen, g1 = d->p[1].gen;
    doc_set_cursor(d, 0, 1);
    doc_insert(d, "x", 1);
    CHECK(d->p[0].gen != g0);
    CHECK(d->p[1].gen == g1);
    doc_free(d);
}

void test_doc_all(void) {
    RUN(test_utf8);
    RUN(test_roundtrip);
    RUN(test_insert_and_split);
    RUN(test_delete);
    RUN(test_cursor_moves);
    RUN(test_word_count);
    RUN(test_gen_changes);
}
