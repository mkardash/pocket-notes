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

void test_keymap_all(void) {
    RUN(test_physical);
    RUN(test_osk_rows);
}
