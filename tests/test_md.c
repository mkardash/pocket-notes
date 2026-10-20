#include "test.h"
#include "../src/core/mdstyle.h"

static ParaStyle ps;

/* Renders the style of each byte as a letter: . plain, m markup, b bold, i italic, B bold+italic,
 * s strike, c code, h highlight, l link, x literal. */
static const char *styles(const char *s, int in_code) {
    static char out[256];
    int len = (int)strlen(s);
    md_parse(s, len, in_code, &ps);
    for (int i = 0; i < len && i < 255; i++) {
        unsigned char st = ps.style[i];
        char c = '.';
        if (st & ST_MARK) c = 'm';
        else if ((st & ST_BOLD) && (st & ST_ITALIC)) c = 'B';
        else if (st & ST_BOLD) c = 'b';
        else if (st & ST_ITALIC) c = 'i';
        else if (st & ST_STRIKE) c = 's';
        else if (st & ST_CODE) c = 'c';
        else if (st & ST_HL) c = 'h';
        else if (st & ST_LINK) c = 'l';
        else if (st & ST_LITERAL) c = 'x';
        out[i] = c;
    }
    out[len < 255 ? len : 255] = 0;
    return out;
}

static void test_blocks(void) {
    styles("# Title", 0);
    CHECK_INT(ps.type, BLK_HEADING); CHECK_INT(ps.level, 1); CHECK_INT(ps.marker_len, 2);
    styles("###### Six", 0);
    CHECK_INT(ps.level, 6);
    styles("####### seven", 0);
    CHECK_INT(ps.type, BLK_NORMAL);
    styles("#hashtag", 0);
    CHECK_INT(ps.type, BLK_NORMAL);
    styles("> quote", 0);
    CHECK_INT(ps.type, BLK_QUOTE); CHECK_INT(ps.level, 1);
    styles("> > nested", 0);
    CHECK_INT(ps.level, 2); CHECK_INT(ps.marker_len, 4);
    styles("- item", 0);
    CHECK_INT(ps.type, BLK_BULLET); CHECK_INT(ps.marker_len, 2); CHECK_INT(ps.bullet_off, 0);
    styles("    * deep", 0);
    CHECK_INT(ps.type, BLK_BULLET); CHECK_INT(ps.level, 2); CHECK_INT(ps.marker_len, 6);
    styles("- [ ] task", 0);
    CHECK_INT(ps.type, BLK_TODO); CHECK_INT(ps.box_off, 2); CHECK_INT(ps.marker_len, 6);
    styles("  - [x] done", 0);
    CHECK_INT(ps.type, BLK_DONE); CHECK_INT(ps.box_off, 4);
    styles("12. twelve", 0);
    CHECK_INT(ps.type, BLK_NUMBER); CHECK_INT(ps.number, 12); CHECK_INT(ps.marker_len, 4);
    styles("3) three", 0);
    CHECK_INT(ps.type, BLK_NUMBER);
    styles("2026. рік", 0);
    CHECK_INT(ps.type, BLK_NUMBER);      /* CommonMark: up to 9 digits */
    styles("---", 0);
    CHECK_INT(ps.type, BLK_HR);
    styles("* * *", 0);
    CHECK_INT(ps.type, BLK_HR);
    styles("— Діалог", 0);
    CHECK_INT(ps.type, BLK_NORMAL);
    styles("", 0);
    CHECK_INT(ps.type, BLK_EMPTY);
    styles("| a | b |", 0);
    CHECK_INT(ps.type, BLK_TABLE);
    CHECK_STR(styles("| a | b |", 0), "m...m...m");
    styles("|---|:-:|", 0);
    CHECK_INT(ps.type, BLK_TABLE_RULE);
}

static void test_code(void) {
    CHECK(md_is_fence("```", 3));
    CHECK(md_is_fence("```python", 9));
    CHECK(md_is_fence("~~~", 3));
    CHECK(!md_is_fence("``", 2));
    styles("```c", 0);
    CHECK_INT(ps.type, BLK_FENCE);
    CHECK_STR(styles("x = *y*", 1), "ccccccc");
    CHECK_INT(ps.type, BLK_CODE);
    CHECK_STR(styles("a `*b*` c", 0), "..mcccm..");
}

static void test_inline(void) {
    CHECK_STR(styles("a **bold** b", 0), "..mmbbbbmm..");
    CHECK_STR(styles("*it* x", 0), "miim..");
    CHECK_STR(styles("***both***", 0), "mmmBBBBmmm");
    CHECK_STR(styles("~~gone~~", 0), "mmssssmm");
    CHECK_STR(styles("==mark==", 0), "mmhhhhmm");
    CHECK_STR(styles("snake_case_name", 0), "...............");
    CHECK_STR(styles("_it_", 0), "miim");
    CHECK_STR(styles("2 * 3 * 4", 0), ".........");
    CHECK_STR(styles("\\*not\\*", 0), "mx...mx");
    CHECK_STR(styles("# **H**", 0), "mmmmbmm");
}

static void test_links(void) {
    CHECK_STR(styles("[site](http://a)", 0), "mllllmmmmmmmmmmm");
    CHECK_STR(styles("![pic](a.png)", 0), "mmiiimmmmmmmm");
    CHECK_STR(styles("<http://x.y>", 0), "mllllllllllm");
    CHECK_STR(styles("note[^1]", 0), "....mmmm");
    CHECK_STR(styles("[not a link]", 0), "............");
}

static void test_prefix(void) {
    char p[32];
    md_prefix_for(FMT_TODO, 2, 0, p, sizeof p);
    CHECK_STR(p, "  - [ ] ");
    md_prefix_for(FMT_NUMBER, 0, 3, p, sizeof p);
    CHECK_STR(p, "3. ");
    md_prefix_for(FMT_H2, 4, 0, p, sizeof p);
    CHECK_STR(p, "## ");
    md_prefix_for(FMT_NORMAL, 0, 0, p, sizeof p);
    CHECK_STR(p, "");
}

void test_md_all(void) {
    RUN(test_blocks);
    RUN(test_code);
    RUN(test_inline);
    RUN(test_links);
    RUN(test_prefix);
    md_free(&ps);
}
