#include "test.h"

int test_failures = 0;
int test_checks = 0;

void test_doc_all(void);
void test_layout_all(void);
void test_book_all(void);
void test_keymap_all(void);
void test_md_all(void);
void test_fmtview_all(void);
void test_fmtedit_all(void);

int main(void) {
    test_doc_all();
    test_layout_all();
    test_book_all();
    test_keymap_all();
    test_md_all();
    test_fmtview_all();
    test_fmtedit_all();
    if (test_failures) {
        fprintf(stderr, "%d of %d checks FAILED\n", test_failures, test_checks);
        return 1;
    }
    printf("All %d checks passed\n", test_checks);
    return 0;
}
