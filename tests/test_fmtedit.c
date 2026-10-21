#include "test.h"
#include "../src/core/mdstyle.h"
#include "../src/core/fmtview.h"
#include "../src/core/fmtedit.h"

static FeOut out;

/* Types t at the '^' in `before` (snapped to the canonical spot first) with typing style want.
 * Returns the new text with '^' at the new cursor. */
static const char *ins(const char *before, const char *t, int want) {
    static char res[512];
    char buf[512];
    unsigned char hid[512];
    ParaStyle ps = {0};
    int len = 0, off = 0;
    for (const char *p = before; *p; p++) {
        if (*p == '^') off = len;
        else buf[len++] = *p;
    }
    buf[len] = 0;
    md_parse(buf, len, 0, &ps);
    fv_hidden(buf, len, &ps, hid);
    off = fv_snap(buf, len, &ps, hid, off);
    md_free(&ps);
    fe_insert(buf, len, 0, off, t, (int)strlen(t), want, &out);
    int c = out.cursor;
    memcpy(res, out.s, c);
    res[c] = '^';
    memcpy(res + c + 1, out.s + c, out.len - c);
    res[out.len + 1] = 0;
    return res;
}

/* Visible content of the last result, with '^' at the cursor. */
static const char *shown(void) {
    static char res[512];
    unsigned char hid[512];
    ParaStyle ps = {0};
    int n = 0;
    md_parse(out.s, out.len, 0, &ps);
    fv_hidden(out.s, out.len, &ps, hid);
    for (int i = fv_content_start(&ps); i <= out.len; i++) {
        if (i == out.cursor) res[n++] = '^';
        if (i < out.len && !hid[i]) res[n++] = out.s[i];
    }
    res[n] = 0;
    md_free(&ps);
    return res;
}

/* Types one letter at the end of an n-byte plain paragraph. */
static void ins_long_plain(int n) {
    char *buf = malloc(n + 1);
    for (int i = 0; i < n; i++) buf[i] = i % 6 == 5 ? ' ' : 'a' + i % 26;
    buf[n] = 0;
    fe_insert(buf, n, 0, n, "x", 1, 0, &out);
    free(buf);
}

static void test_fe_insert(void) {
    CHECK_STR(ins("ab^", "c", 0), "abc^");
    CHECK_STR(ins("**bold^**", "x", ST_BOLD), "**boldx^**");
    CHECK_STR(ins("^**b**", "x", 0), "x^**b**");
    CHECK_STR(ins("**hello^**", " ", ST_BOLD), "**hello** ^");
    CHECK_STR(ins("**hello** ^", "w", ST_BOLD), "**hello w^**");        /* style carries across the space */
    CHECK_STR(ins("**hello** ^", "w", 0), "**hello** w^");
    CHECK_STR(ins("a ^", "b", ST_BOLD), "a **b^**");                    /* B menu toggled on */
    CHECK_STR(ins("**hello^**", " ", 0), "**hello** ^");                /* toggled off at run end */
    CHECK_STR(ins("**ab^**", "c", 0), "**ab**c^");
    CHECK_STR(ins("**he^llo**", "x", 0), "**he**x^**llo**");            /* toggled off mid-run */
    CHECK_STR(ins("wo^rd", "x", ST_ITALIC), "wo*x^*rd");
    CHECK_STR(ins("[t](u)^", "x", 0), "[t](u)x^");
    CHECK_STR(ins("[t^e](u)", "x", 0), "[tx^e](u)");                    /* tier C */
    CHECK_STR(ins("`rain^`", "x", ST_CODE), "`rainx^`");
    CHECK_STR(ins("*b^", "*", 0), "*b^*");                              /* hand-typed pair → italic */
    CHECK_STR(ins("*fo^o*", "*", ST_ITALIC), "*fo\\*^o*");              /* tier E keeps the italic */
    CHECK_STR(ins("^_word_", "x", 0), "x^*word*");                      /* tier F */
    CHECK_STR(ins("^", "-", 0), "-^");
    CHECK_STR(ins("-^", " ", 0), "- ^");                                /* AutoFormat bullet */
    CHECK_STR(ins("**При^**", "в", ST_BOLD), "**Прив^**");
    CHECK_STR(ins("**not closed^", "x", 0), "**not closedx^");          /* unbalanced stays literal */
    CHECK_STR(ins("===^", "=", 0), "====^");
    fe_parse_count = 0; ins_long_plain(4000); CHECK(fe_parse_count <= 2); /* 4 KB plain paragraph, cursor at end */
    CHECK_INT(out.len, 4001); CHECK_INT(out.cursor, 4001);

    /* Underscores in a link destination are not emphasis markers and stay as they are. */
    CHECK_STR(ins("^_word_ [t](u_v)", "x", 0), "x^*word* [t](u_v)");
    /* Typing never goes inside a link destination, even where a table shows its pipes. */
    CHECK_STR(ins("| [t](u)^ |", "|", 0), "| [t](u)|^ |");
    /* Last resort: an empty heading "#" turns into plain text. */
    CHECK_STR(ins("#^", "x", 0), "#x^");
    /* A second typed space with Bold on goes in plain, then the closer moves past the letter. */
    CHECK_STR(ins("**hello** ^", " ", ST_BOLD), "**hello**  ^");
    CHECK_STR(ins("**hello**  ^", "w", ST_BOLD), "**hello  w^**");

    /* A marker typed after a closer of its kind: the typed byte is the visible one, cursor after it. */
    CHECK_STR(ins("**a**^", "*", 0), "**a***^");        CHECK_STR(shown(), "a*^");
    CHECK_STR(ins("**a**^", "*", ST_BOLD), "**a***^");  CHECK_STR(shown(), "a*^");
    CHECK_STR(ins("**a**^", "**", 0), "**a****^");      CHECK_STR(shown(), "a**^");
    CHECK_STR(ins("==hl==^", "=", 0), "==hl===^");      CHECK_STR(shown(), "hl=^");
    CHECK_STR(ins("~~s~~^", "~", 0), "~~s~~~^");        CHECK_STR(shown(), "s~^");
    CHECK_STR(ins("`c`^", "`", 0), "`c``^");            CHECK_STR(shown(), "c`^");
    CHECK_STR(ins("_a_^", "_", 0), "*a*_^");            CHECK_STR(shown(), "a_^");
    CHECK_STR(ins("*a*_^", "b", 0), "*a*_b^");          CHECK_STR(shown(), "a_b^");
}

void test_fmtedit_all(void) {
    RUN(test_fe_insert);
    fe_free(&out);
}
