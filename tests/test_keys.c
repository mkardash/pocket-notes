#include "test.h"
#include "../src/core/keys.h"

static void test_physical(void) {
    CHECK_STR(keys_physical_char(16, LAYOUT_UK, 0), "й");
    CHECK_STR(keys_physical_char(16, LAYOUT_UK, 1), "Й");
    CHECK_STR(keys_physical_char(16, LAYOUT_EN, 0), "q");
    CHECK_STR(keys_physical_char(27, LAYOUT_UK, 0), "ї");
    CHECK_STR(keys_physical_char(40, LAYOUT_UK, 0), "є");
    CHECK_STR(keys_physical_char(43, LAYOUT_UK, 1), "Ґ");
    CHECK_STR(keys_physical_char(41, LAYOUT_UK, 0), "'");
    CHECK_STR(keys_physical_char(53, LAYOUT_UK, 1), ",");
    CHECK_STR(keys_physical_char(4, LAYOUT_UK, 1), "№");
    CHECK_STR(keys_physical_char(57, LAYOUT_EN, 0), " ");
    CHECK(keys_physical_char(KC_ENTER, LAYOUT_EN, 0) == NULL);
    CHECK(keys_physical_char(500, LAYOUT_EN, 0) == NULL);
    CHECK_STR(keys_alt_char(12), "—");
    CHECK_STR(keys_alt_char(51), "«");
    CHECK(keys_alt_char(16) == NULL);
}

static void test_osk_rows(void) {
    for (int page = 0; page <= OSK_PAGE_SYMBOLS; page++) {
        const OskLayout *l = keys_osk_page(page);
        CHECK_INT(l->nrows, 5);
        for (int r = 0; r < l->nrows; r++) {
            int sum = 0;
            for (int k = 0; k < l->rows[r].n; k++) sum += l->rows[r].keys[k].weight;
            if (sum != 120) fprintf(stderr, "page %d row %d sums to %d\n", page, r, sum);
            CHECK_INT(sum, 120);
        }
    }
}

static void test_layouts(void) {
    CHECK_INT(keys_next_layout(LAYOUT_EN, LAYOUTS_DEFAULT), LAYOUT_UK);
    CHECK_INT(keys_next_layout(LAYOUT_UK, LAYOUTS_DEFAULT), LAYOUT_EN);
    unsigned m = LAYOUTS_DEFAULT | (1u << LAYOUT_DE) | (1u << LAYOUT_RU);
    CHECK_INT(keys_next_layout(LAYOUT_EN, m), LAYOUT_DE);
    CHECK_INT(keys_next_layout(LAYOUT_DE, m), LAYOUT_RU);
    CHECK_INT(keys_next_layout(LAYOUT_RU, m), LAYOUT_UK);
    CHECK_INT(keys_next_layout(LAYOUT_FR, 1u << LAYOUT_FR), LAYOUT_FR);
    CHECK_STR(keys_layout_short(LAYOUT_DE), "DEU"); CHECK_STR(keys_layout_name(LAYOUT_FR), "Français");
    CHECK_STR(keys_layout_short(LAYOUT_RU), "РУС"); CHECK_INT(keys_layout_cyrillic(LAYOUT_RU), 1);
    CHECK_INT(keys_layout_cyrillic(LAYOUT_PL), 0);
    CHECK_STR(keys_physical_char(21, LAYOUT_DE, 0), "z"); CHECK_STR(keys_physical_char(26, LAYOUT_DE, 1), "Ü");
    CHECK_STR(keys_physical_char(44, LAYOUT_DE, 0), "y"); CHECK_STR(keys_physical_char(12, LAYOUT_DE, 0), "ß");
    CHECK_STR(keys_physical_char(16, LAYOUT_FR, 0), "a"); CHECK_STR(keys_physical_char(39, LAYOUT_ES, 0), "ñ");
    CHECK_STR(keys_physical_char(17, LAYOUT_FR, 0), "z"); CHECK_STR(keys_physical_char(30, LAYOUT_FR, 0), "q");
    CHECK_STR(keys_physical_char(39, LAYOUT_FR, 0), "m"); CHECK_STR(keys_physical_char(44, LAYOUT_FR, 0), "w");
    CHECK_STR(keys_physical_char(50, LAYOUT_FR, 0), ","); CHECK_STR(keys_physical_char(3, LAYOUT_FR, 0), "é");
    CHECK_STR(keys_physical_char(3, LAYOUT_FR, 1), "2"); CHECK_STR(keys_physical_char(11, LAYOUT_FR, 0), "à");
    CHECK_STR(keys_physical_char(16, LAYOUT_RU, 0), "й"); CHECK_STR(keys_physical_char(41, LAYOUT_RU, 0), "ё");
    CHECK_STR(keys_physical_char(16, LAYOUT_PL, 0), "q"); CHECK_STR(keys_physical_char(57, LAYOUT_DE, 0), " ");
}

static void test_new_pages(void) {
    int has_ue = 0, has_yo = 0;
    const OskLayout *de = keys_osk_page(LAYOUT_DE), *ru = keys_osk_page(LAYOUT_RU);
    for (int k = 0; k < de->rows[1].n; k++) if (de->rows[1].keys[k].lower && !strcmp(de->rows[1].keys[k].lower, "ü")) has_ue = 1;
    for (int k = 0; k < ru->rows[2].n; k++) if (ru->rows[2].keys[k].lower && !strcmp(ru->rows[2].keys[k].lower, "ё")) has_yo = 1;
    CHECK(has_ue); CHECK(has_yo);
    CHECK(keys_osk_page(99) == keys_osk_page(OSK_PAGE_UK));
    CHECK_INT(OSK_PAGE_SYMBOLS, 7);
}

static void test_variants(void) {
    const char *v[16];
    int n = keys_variants("e", v, 16);
    CHECK_INT(n, 6); CHECK_STR(v[0], "é"); CHECK_STR(v[4], "ę");
    n = keys_variants("E", v, 16); CHECK_INT(n, 6); CHECK_STR(v[0], "É");
    CHECK_INT(keys_variants("x", v, 16), 0); CHECK_INT(keys_variants("?", v, 16), 1);
    CHECK_STR(v[0], "¿");
    CHECK_INT(keys_variants("!", v, 16), 1); CHECK_STR(v[0], "¡");
    CHECK_INT(keys_variants("s", v, 16), 3); CHECK_STR(v[2], "ß");
    CHECK_INT(keys_variants("S", v, 16), 2);
    CHECK_INT(keys_variants("a", v, 3), 3);
    CHECK_INT(keys_variants("ab", v, 16), 0);
}

void test_keymap_all(void) {
    RUN(test_physical);
    RUN(test_osk_rows);
    RUN(test_layouts);
    RUN(test_new_pages);
    RUN(test_variants);
}
