#include "test.h"
#include "../src/core/mdstyle.h"
#include "../src/core/fmtview.h"
#include "../src/core/fmtedit.h"

static FeOut out;

/* Copies `before` to buf without its '^' and returns the '^' offset snapped to the canonical
 * cursor spot. */
static int take_cursor(const char *before, char *buf, int *len) {
    unsigned char hid[512];
    ParaStyle ps = {0};
    int n = 0, off = 0;
    for (const char *p = before; *p; p++) {
        if (*p == '^') off = n;
        else buf[n++] = *p;
    }
    buf[n] = 0;
    md_parse(buf, n, 0, &ps);
    fv_hidden(buf, n, &ps, hid);
    off = fv_snap(buf, n, &ps, hid, off);
    md_free(&ps);
    *len = n;
    return off;
}

/* The last result's text with '^' at its cursor. */
static const char *with_cursor(void) {
    static char res[512];
    int c = out.cursor;
    memcpy(res, out.s, c);
    res[c] = '^';
    memcpy(res + c + 1, out.s + c, out.len - c);
    res[out.len + 1] = 0;
    return res;
}

/* Types t at the '^' in `before` (snapped to the canonical spot first) with typing style want.
 * Returns the new text with '^' at the new cursor. */
static const char *ins(const char *before, const char *t, int want) {
    char buf[512];
    int len, off = take_cursor(before, buf, &len);
    fe_insert(buf, len, 0, off, t, (int)strlen(t), want, &out);
    return with_cursor();
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

/* Block type of the last result. */
static int shown_type(void) {
    ParaStyle ps = {0};
    md_parse(out.s, out.len, 0, &ps);
    int t = ps.type;
    md_free(&ps);
    return t;
}

/* Inline style (FV_INLINE | ST_LINK bits) of the first visible content byte c of the last
 * result, or -1 when no visible byte is c. */
static int style_of(char c) {
    unsigned char hid[512];
    ParaStyle ps = {0};
    int st = -1;
    md_parse(out.s, out.len, 0, &ps);
    fv_hidden(out.s, out.len, &ps, hid);
    for (int i = fv_content_start(&ps); i < out.len && st < 0; i++)
        if (!hid[i] && out.s[i] == c) st = ps.style[i] & (FV_INLINE | ST_LINK);
    md_free(&ps);
    return st;
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

/* Deletes the visible character before (fwd 0) or after (fwd 1) the '^' in `before`. Returns
 * the new text with '^' at the cursor, or "(none)" when there is nothing to delete. */
static int del_rc;
static const char *del_dir(const char *before, int fwd) {
    char buf[512];
    int len, off = take_cursor(before, buf, &len);
    del_rc = fwd ? fe_delete_forward(buf, len, 0, off, &out) : fe_delete_back(buf, len, 0, off, &out);
    return del_rc ? with_cursor() : "(none)";
}
static const char *del(const char *before) { return del_dir(before, 0); }
static const char *delf(const char *before) { return del_dir(before, 1); }
static int delf_rc(const char *before) { del_dir(before, 1); return del_rc; }

/* Enter at the '^': returns "left/right". */
static const char *split(const char *before) {
    static char res[1024];
    char buf[512];
    int len, off = take_cursor(before, buf, &len);
    fe_split(buf, len, 0, off, &out);
    snprintf(res, sizeof res, "%.*s/%.*s", out.len, out.s, out.len2, out.s2);
    return res;
}

/* Joins b to a: returns the text with '^' at the join point. */
static const char *join(const char *a, const char *b) {
    fe_join(a, (int)strlen(a), b, (int)strlen(b), &out);
    return with_cursor();
}

/* Do both halves of the last split show no markup character, with every visible letter
 * styled bits? */
static int halves_clean(int bits) {
    const char *t[2] = { out.s, out.s2 };
    int n[2] = { out.len, out.len2 }, ok = 1;
    for (int h = 0; h < 2; h++) {
        unsigned char hid[512];
        ParaStyle ps = {0};
        md_parse(t[h], n[h], 0, &ps);
        fv_hidden(t[h], n[h], &ps, hid);
        for (int i = fv_content_start(&ps); i < n[h]; i++) {
            if (hid[i] || t[h][i] == ' ') continue;
            if (strchr("*_~=`[]()", t[h][i]) || (ps.style[i] & bits) != bits) ok = 0;
        }
        md_free(&ps);
    }
    return ok;
}

/* Toggles bit on the visible bytes [a, b) of s: returns the new text, or "(none)". */
static const char *restyle(const char *s, int a, int b, int bit) {
    if (!fe_restyle(s, (int)strlen(s), 0, a, b, bit, &out)) return "(none)";
    return out.s;
}

/* Deletes the last letter of an n-byte plain paragraph. */
static void del_long_plain(int n) {
    char *buf = malloc(n + 1);
    for (int i = 0; i < n; i++) buf[i] = i % 6 == 5 ? ' ' : 'a' + i % 26;
    buf[n] = 0;
    fe_delete_back(buf, n, 0, n, &out);
    free(buf);
}

static void test_fe_delete(void) {
    CHECK_STR(del("ab^"), "a^");
    CHECK_STR(del("**b^**"), "^");
    CHECK_STR(del("x **b^** y"), "x ^ y");
    CHECK_STR(del("**ab^**"), "**a^**");
    CHECK_STR(del("***b^***"), "^");
    CHECK_STR(del("[t^](u)"), "^");                 /* emptied link goes entirely */
    CHECK_STR(del("a ^_word_"), "a^*word*");        /* removing the space would break _..._ */
    CHECK_STR(del("**Пр^**"), "**П^**");
    CHECK_STR(delf("^ab"), "^b");
    CHECK_STR(delf("a^**b**"), "a^");
    CHECK_INT(delf_rc("**ab^**"), 0);              /* nothing visible after the cursor */

    /* Nothing to delete: returns 0 and leaves o as it was. */
    CHECK_STR(del("ab^"), "a^");
    CHECK_STR(del("^ab"), "(none)");
    CHECK_STR(with_cursor(), "a^");
    CHECK_STR(del("# ^ab"), "(none)");              /* the block prefix is not deleted here */
    CHECK_STR(del("# a^b"), "# ^b");
    CHECK_STR(delf("^Жx"), "^x");
    /* A footnote reference next to an emptied run stays ('^' is not a cursor here). */
    CHECK_INT(fe_delete_back("[^1]**b**", 9, 0, 7, &out), 1);
    CHECK_STR(out.s, "[^1]");
    CHECK_INT(out.cursor, 0);
    fe_parse_count = 0; del_long_plain(4000); CHECK(fe_parse_count <= 2); /* 4 KB plain paragraph */
    CHECK_INT(out.len, 3999); CHECK_INT(out.cursor, 3999);

    /* The first letter of a new word in a styled run: the closer moves back before the space. */
    CHECK_STR(del("**hello w^**"), "**hello** ^");
    CHECK_STR(shown(), "hello ^");
    CHECK_STR(del("**a b^**"), "**a** ^");
    CHECK_STR(del("*a b^*"), "*a* ^");
    CHECK_STR(delf("**hello ^w**"), "**hello** ^");
    CHECK_STR(del("**a *b^***"), "**a** ^");        /* the inner run goes, the outer closer moves back */
    /* The only character between two links: both destinations stay (none goes without its '['). */
    CHECK_STR(del("[x](u) ^[y](v)"), "[x^](u)[y](v)");
    CHECK_STR(del("[x](u)b^[y](v)"), "[x^](u)[y](v)");
    CHECK_STR(delf("[x](u)^b[y](v)"), "[x^](u)[y](v)");
    CHECK_STR(del("![x](u)b^![y](v)"), "![x^](u)![y](v)");
    CHECK_STR(del("[x](u)b^[*y*](v)"), "[x^](u)[*y*](v)");
    /* A link with no text is hidden whole: a cut next to it never takes it. */
    CHECK_STR(del("**x^**[](u)"), "^[](u)");

    /* The whole first word of a styled phrase: the opener moves forward past the space, which
     * loses the style. No marker shows and the rest keeps its style. */
    CHECK_STR(del("**h^ world**"), "^ **world**");
    CHECK_STR(shown(), "^ world");
    CHECK_INT(style_of('w'), ST_BOLD);
    CHECK_STR(del("*h^ world*"), "^ *world*");          /* never "* world*", a bullet */
    CHECK_INT(shown_type(), BLK_NORMAL);
    CHECK_INT(style_of('w'), ST_ITALIC);
    CHECK_STR(del("==h^ world=="), "^ ==world==");
    CHECK_INT(style_of('w'), ST_HL);
    CHECK_STR(del("~~h^ world~~"), "^ ~~world~~");
    CHECK_INT(style_of('w'), ST_STRIKE);
    CHECK_STR(del("_h^ world_"), "^ _world_");
    CHECK_INT(style_of('w'), ST_ITALIC);
    CHECK_STR(del("__h^ world__"), "^ __world__");
    CHECK_INT(style_of('w'), ST_BOLD);
    CHECK_STR(del("**Ж^ світ**"), "^ **світ**");
    CHECK_STR(shown(), "^ світ");
    CHECK_STR(del("a **h^ world** b"), "a ^ **world** b");
    CHECK_STR(del("# **h^ world**"), "# ^ **world**");
    CHECK_INT(shown_type(), BLK_HEADING);
    CHECK_STR(del("- **h^ world**"), "- ^ **world**");
    CHECK_INT(shown_type(), BLK_BULLET);
    CHECK_STR(del("- [x] *h^ world*"), "- [x] ^ *world*");
    CHECK_INT(shown_type(), BLK_DONE);
    CHECK_STR(del("1. ==h^ world=="), "1. ^ ==world==");
    CHECK_INT(shown_type(), BLK_NUMBER);
    CHECK_STR(delf("**^h world**"), "^ **world**");
    CHECK_STR(delf("*^h world*"), "^ *world*");
    CHECK_INT(shown_type(), BLK_NORMAL);
    CHECK_STR(delf("# ~~^h world~~"), "# ^ ~~world~~");
    CHECK_STR(delf("- __^h world__"), "- ^ __world__");
    /* In a quote the prefix takes the space, which is still drawn there. */
    CHECK_STR(del("> **h^ world**"), ">  ^**world**");
    CHECK_INT(shown_type(), BLK_QUOTE);
    CHECK_INT(style_of('w'), ST_BOLD);
    CHECK_STR(delf("> > ==^h world=="), "> >  ^==world==");
    CHECK_INT(style_of('w'), ST_HL);
    CHECK_STR(del("> **h^** world"), ">  ^world");
    /* Only the run's own opener moves; the closer before it stays. */
    CHECK_STR(del("~~c~~**h^ w**"), "~~c^~~ **w**");
    CHECK_INT(style_of('c'), ST_STRIKE);
    CHECK_INT(style_of('w'), ST_BOLD);

    /* The closer that moves back is only the run's own: the next run keeps its opener. */
    CHECK_STR(del("**a b^**~~c~~"), "**a** ^~~c~~");
    CHECK_STR(shown(), "a ^c");
    CHECK_INT(style_of('c'), ST_STRIKE);
    CHECK_STR(del("**a b^***c*"), "**a** ^*c*");
    CHECK_STR(shown(), "a ^c");
    CHECK_INT(style_of('c'), ST_ITALIC);
    CHECK_STR(del("**a b^**[c](u)"), "**a** ^[c](u)");
    CHECK_STR(shown(), "a ^c");
    CHECK_INT(style_of(' '), 0);                        /* the space is not link text */
    CHECK_INT(style_of('c'), ST_LINK);
}

static void test_fe_split(void) {
    CHECK_STR(split("ab^cd"), "ab/cd");
    CHECK_STR(split("**bo^ld**"), "**bo**/**ld**");
    CHECK_STR(split("**ab^**cd"), "**ab**/cd");
    CHECK_STR(split("**a *b^c***"), "**a *b***/***c***");
    CHECK_STR(split("~~a^b~~"), "~~a~~/~~b~~");
    CHECK_STR(split("[li^nk](u) x"), "[link](u)/ x");
    CHECK_STR(split("**Ж^ж**"), "**Ж**/**ж**");

    CHECK_STR(split("# He^ad"), "# He/ad");
    CHECK_STR(split("ab^"), "ab/");
    CHECK_STR(split("[a](u)^[b](v)"), "[a](u)/[b](v)");   /* between two links */
    /* Next to whitespace in a styled run: closers before it on the left, openers after it on the right. */
    CHECK_STR(split("**hello^ world**"), "**hello**/ **world**");
    CHECK(halves_clean(ST_BOLD));
    CHECK_STR(split("**hello ^world**"), "**hello** /**world**");
    CHECK(halves_clean(ST_BOLD));
    CHECK_STR(split("*a^ b*"), "*a*/ *b*");
    CHECK(halves_clean(ST_ITALIC));
    /* After a link in a styled run: the opener run "**[" is trimmed to its marker prefix "**". */
    CHECK_STR(split("**[li^nk](u) x**"), "**[link](u)**/ **x**");
    CHECK(halves_clean(ST_BOLD));
    /* Typing after a split makes one paragraph again. */
    ins("a^", "b", 0);
    CHECK_INT(out.len2, 0);
}

static void test_fe_join(void) {
    CHECK_STR(join("**a**", "**b**"), "**a^****b**");
    CHECK_STR(join("a", "- b"), "a^b");
    CHECK_STR(join("_a_", "b"), "*a^*b");

    CHECK_STR(join("# a", "## b"), "# a^b");
    CHECK_STR(join("", "- b"), "^b");
}

static void test_fe_restyle(void) {
    CHECK_STR(restyle("a word b", 2, 6, ST_BOLD), "a **word** b");
    CHECK_STR(restyle("a **word** b", 4, 8, ST_BOLD), "a word b");
    CHECK_STR(restyle("**very bold**", 7, 11, ST_BOLD), "**very** bold");
    CHECK_STR(restyle("**bold very**", 2, 6, ST_BOLD), "bold **very**");
    CHECK_STR(restyle("**a word b**", 4, 8, ST_BOLD), "**a** word **b**");
    CHECK_STR(restyle("**a word b**", 4, 8, ST_ITALIC), "**a *word* b**");

    CHECK_STR(restyle("*word*", 1, 5, ST_ITALIC), "word");
    CHECK_STR(restyle("a word", 2, 6, ST_STRIKE), "a ~~word~~");
    CHECK_STR(restyle("_word_", 1, 5, ST_ITALIC), "word");       /* R1 on the '_'→'*' text */
    CHECK_STR(restyle("__word__", 2, 6, ST_BOLD), "word");
    /* No candidate renders right (markers are literal in a code block): o stays as it was. */
    CHECK_STR(join("a", "b"), "a^b");
    CHECK_INT(fe_restyle("word", 4, 1, 0, 4, ST_BOLD, &out), 0);
    CHECK_STR(with_cursor(), "a^b");
}

void test_fmtedit_all(void) {
    RUN(test_fe_insert);
    RUN(test_fe_delete);
    RUN(test_fe_split);
    RUN(test_fe_join);
    RUN(test_fe_restyle);
    fe_free(&out);
}
