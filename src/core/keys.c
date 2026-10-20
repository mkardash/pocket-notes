#include "keys.h"
#include <stddef.h>

typedef struct { const char *lo, *up; } Pair;

/* US QWERTY. Index = Linux key code. */
static const Pair en_map[64] = {
    [2] = {"1", "!"}, [3] = {"2", "@"}, [4] = {"3", "#"}, [5] = {"4", "$"}, [6] = {"5", "%"},
    [7] = {"6", "^"}, [8] = {"7", "&"}, [9] = {"8", "*"}, [10] = {"9", "("}, [11] = {"0", ")"},
    [12] = {"-", "_"}, [13] = {"=", "+"},
    [16] = {"q", "Q"}, [17] = {"w", "W"}, [18] = {"e", "E"}, [19] = {"r", "R"}, [20] = {"t", "T"},
    [21] = {"y", "Y"}, [22] = {"u", "U"}, [23] = {"i", "I"}, [24] = {"o", "O"}, [25] = {"p", "P"},
    [26] = {"[", "{"}, [27] = {"]", "}"},
    [30] = {"a", "A"}, [31] = {"s", "S"}, [32] = {"d", "D"}, [33] = {"f", "F"}, [34] = {"g", "G"},
    [35] = {"h", "H"}, [36] = {"j", "J"}, [37] = {"k", "K"}, [38] = {"l", "L"},
    [39] = {";", ":"}, [40] = {"'", "\""}, [41] = {"`", "~"}, [43] = {"\\", "|"},
    [44] = {"z", "Z"}, [45] = {"x", "X"}, [46] = {"c", "C"}, [47] = {"v", "V"}, [48] = {"b", "B"},
    [49] = {"n", "N"}, [50] = {"m", "M"}, [51] = {",", "<"}, [52] = {".", ">"}, [53] = {"/", "?"},
    [57] = {" ", " "},
};

/* Ukrainian (Windows "Ukrainian Enhanced": ` types the apostrophe, \ types ґ). */
static const Pair uk_map[64] = {
    [2] = {"1", "!"}, [3] = {"2", "\""}, [4] = {"3", "№"}, [5] = {"4", ";"}, [6] = {"5", "%"},
    [7] = {"6", ":"}, [8] = {"7", "?"}, [9] = {"8", "*"}, [10] = {"9", "("}, [11] = {"0", ")"},
    [12] = {"-", "_"}, [13] = {"=", "+"},
    [16] = {"й", "Й"}, [17] = {"ц", "Ц"}, [18] = {"у", "У"}, [19] = {"к", "К"}, [20] = {"е", "Е"},
    [21] = {"н", "Н"}, [22] = {"г", "Г"}, [23] = {"ш", "Ш"}, [24] = {"щ", "Щ"}, [25] = {"з", "З"},
    [26] = {"х", "Х"}, [27] = {"ї", "Ї"},
    [30] = {"ф", "Ф"}, [31] = {"і", "І"}, [32] = {"в", "В"}, [33] = {"а", "А"}, [34] = {"п", "П"},
    [35] = {"р", "Р"}, [36] = {"о", "О"}, [37] = {"л", "Л"}, [38] = {"д", "Д"},
    [39] = {"ж", "Ж"}, [40] = {"є", "Є"}, [41] = {"'", "₴"}, [43] = {"ґ", "Ґ"},
    [44] = {"я", "Я"}, [45] = {"ч", "Ч"}, [46] = {"с", "С"}, [47] = {"м", "М"}, [48] = {"и", "И"},
    [49] = {"т", "Т"}, [50] = {"ь", "Ь"}, [51] = {"б", "Б"}, [52] = {"ю", "Ю"}, [53] = {".", ","},
    [57] = {" ", " "},
};

const char *keys_physical_char(int keycode, KbLayout layout, int shift) {
    if (keycode < 0 || keycode >= 64) return NULL;
    const Pair *p = layout == LAYOUT_UK ? &uk_map[keycode] : &en_map[keycode];
    if (!p->lo) return NULL;
    return shift ? p->up : p->lo;
}

const char *keys_alt_char(int keycode) {
    switch (keycode) {
    case 12: return "—";   /* - */
    case 51: return "«";   /* , */
    case 52: return "»";   /* . */
    case 53: return "…";   /* / */
    case 40: return "’";   /* ' */
    default: return NULL;
    }
}

#define C(lo, up) { OSK_CHAR, lo, up, 10 }
#define CW(lo, up, w) { OSK_CHAR, lo, up, w }
#define K(type, w) { type, NULL, NULL, w }

/* Shared top row: cursor keys and the punctuation prose needs most. */
static const OskKey top_row[] = {
    K(OSK_LEFT, 10), K(OSK_RIGHT, 10), K(OSK_UP, 10), K(OSK_DOWN, 10),
    C("«", NULL), C("»", NULL), C("—", NULL), C("-", NULL), C("!", NULL), C("?", NULL),
    C(":", NULL), K(OSK_HIDE, 10),
};

static const OskKey uk_r1[] = {
    C("й", "Й"), C("ц", "Ц"), C("у", "У"), C("к", "К"), C("е", "Е"), C("н", "Н"),
    C("г", "Г"), C("ш", "Ш"), C("щ", "Щ"), C("з", "З"), C("х", "Х"), C("ї", "Ї"),
};
static const OskKey uk_r2[] = {
    C("ф", "Ф"), C("і", "І"), C("в", "В"), C("а", "А"), C("п", "П"), C("р", "Р"),
    C("о", "О"), C("л", "Л"), C("д", "Д"), C("ж", "Ж"), C("є", "Є"), C("ґ", "Ґ"),
};
static const OskKey uk_r3[] = {
    K(OSK_SHIFT, 10), C("я", "Я"), C("ч", "Ч"), C("с", "С"), C("м", "М"), C("и", "И"),
    C("т", "Т"), C("ь", "Ь"), C("б", "Б"), C("ю", "Ю"), C("'", NULL), K(OSK_BACKSPACE, 10),
};
static const OskKey bottom_letters[] = {
    K(OSK_SYMBOLS, 15), K(OSK_LANG, 15), C(",", NULL), K(OSK_SPACE, 50), C(".", NULL), K(OSK_ENTER, 20),
};

static const OskKey en_r1[] = {
    CW("q", "Q", 12), CW("w", "W", 12), CW("e", "E", 12), CW("r", "R", 12), CW("t", "T", 12),
    CW("y", "Y", 12), CW("u", "U", 12), CW("i", "I", 12), CW("o", "O", 12), CW("p", "P", 12),
};
static const OskKey en_r2[] = {
    CW("a", "A", 12), CW("s", "S", 12), CW("d", "D", 12), CW("f", "F", 12), CW("g", "G", 12),
    CW("h", "H", 12), CW("j", "J", 12), CW("k", "K", 12), CW("l", "L", 12), CW("'", NULL, 12),
};
static const OskKey en_r3[] = {
    K(OSK_SHIFT, 18), CW("z", "Z", 12), CW("x", "X", 12), CW("c", "C", 12), CW("v", "V", 12),
    CW("b", "B", 12), CW("n", "N", 12), CW("m", "M", 12), K(OSK_BACKSPACE, 18),
};

static const OskKey sym_r1[] = {
    CW("1", NULL, 12), CW("2", NULL, 12), CW("3", NULL, 12), CW("4", NULL, 12), CW("5", NULL, 12),
    CW("6", NULL, 12), CW("7", NULL, 12), CW("8", NULL, 12), CW("9", NULL, 12), CW("0", NULL, 12),
};
static const OskKey sym_r2[] = {
    CW("-", NULL, 12), CW("/", NULL, 12), CW(":", NULL, 12), CW(";", NULL, 12), CW("(", NULL, 12),
    CW(")", NULL, 12), CW("\"", NULL, 12), CW("'", NULL, 12), CW("&", NULL, 12), CW("@", NULL, 12),
};
static const OskKey sym_r3[] = {
    C("*", NULL), C("%", NULL), C("№", NULL), C("#", NULL), C("+", NULL), C("=", NULL),
    C("_", NULL), C("…", NULL), C("“", NULL), C("”", NULL), K(OSK_BACKSPACE, 20),
};
static const OskKey bottom_symbols[] = {
    K(OSK_LETTERS, 15), K(OSK_LANG, 15), C(",", NULL), K(OSK_SPACE, 50), C(".", NULL), K(OSK_ENTER, 20),
};

#define ROW(a) { a, (int)(sizeof(a) / sizeof(a[0])) }
static const OskRow uk_rows[] = { ROW(top_row), ROW(uk_r1), ROW(uk_r2), ROW(uk_r3), ROW(bottom_letters) };
static const OskRow en_rows[] = { ROW(top_row), ROW(en_r1), ROW(en_r2), ROW(en_r3), ROW(bottom_letters) };
static const OskRow sym_rows[] = { ROW(top_row), ROW(sym_r1), ROW(sym_r2), ROW(sym_r3), ROW(bottom_symbols) };

static const OskLayout pages[] = {
    { uk_rows, 5 }, { en_rows, 5 }, { sym_rows, 5 },
};

const OskLayout *keys_osk_page(int page) {
    if (page < 0 || page > OSK_PAGE_SYMBOLS) page = OSK_PAGE_UK;
    return &pages[page];
}
