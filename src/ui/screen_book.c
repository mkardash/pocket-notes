/* Project outline: folders and notes (.md files) as an indented list. */
#include "app.h"
#include "../core/book.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { Node *node; int depth; char preview[160]; } Row;

static char book_dir[1024];
static Node *root = NULL;
static Row *rows = NULL;
static int nrows = 0, rows_cap = 0;
static int page = 0;
static int down_y = -1;
static char menu_path[1024];          /* item the open menu is about (path survives reloads) */
static char collapsed[64][1024];
static int ncollapsed = 0;

static int list_top(void) { return g_header_h; }
static int footer_h(void) { return g_row_h; }
static int rows_per_page(void) { return (SH - list_top() - footer_h()) / g_row_h; }
static int page_count(void) { int r = rows_per_page(); return nrows ? (nrows + r - 1) / r : 1; }
static Rect row_rect(int i) { return rect_make(0, list_top() + i * g_row_h, SW, g_row_h); }
static Rect dots_rect(int i) { Rect r = row_rect(i); return rect_make(SW - g_row_h - g_margin / 2, r.y, g_row_h, g_row_h); }
static Rect pager_prev(void) { return rect_make(0, SH - footer_h(), SW / 4, footer_h()); }
static Rect pager_next(void) { return rect_make(SW * 3 / 4, SH - footer_h(), SW / 4, footer_h()); }

static int is_collapsed(const char *path) {
    for (int i = 0; i < ncollapsed; i++) if (!strcmp(collapsed[i], path)) return 1;
    return 0;
}

static void toggle_collapsed(const char *path) {
    for (int i = 0; i < ncollapsed; i++) {
        if (!strcmp(collapsed[i], path)) {
            memmove(collapsed[i], collapsed[i + 1], sizeof(collapsed[0]) * (ncollapsed - i - 1));
            ncollapsed--;
            return;
        }
    }
    if (ncollapsed < 64) snprintf(collapsed[ncollapsed++], sizeof collapsed[0], "%s", path);
}

/* First line of an idea file, so the outline shows what the idea is about. */
static void read_preview(const char *path, char *out, size_t cap) {
    out[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return;
    char buf[200];
    size_t n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    char *nl = strchr(buf, '\n');
    if (nl) *nl = 0;
    else if (n == sizeof buf - 1) {
        /* The read stopped mid-text: drop a partial UTF-8 character at the cut. */
        size_t len = n;
        while (len > 0 && ((unsigned char)buf[len - 1] & 0xC0) == 0x80) len--;
        if (len > 0 && (unsigned char)buf[len - 1] >= 0xC0) buf[len - 1] = 0;
    }
    snprintf(out, cap, "%s", buf);
}

static void add_row(Node *n, int depth) {
    if (nrows == rows_cap) { rows_cap = rows_cap ? rows_cap * 2 : 64; rows = realloc(rows, sizeof(Row) * rows_cap); }
    rows[nrows].node = n;
    rows[nrows].depth = depth;
    rows[nrows].preview[0] = 0;
    if (n->type == NODE_DOC && n->parent && n->parent->special == SPECIAL_IDEAS)
        read_preview(n->path, rows[nrows].preview, sizeof rows[nrows].preview);
    nrows++;
}

static void flatten(Node *n, int depth) {
    for (int i = 0; i < n->nkids; i++) {
        Node *k = n->kids[i];
        add_row(k, depth);
        if (k->type == NODE_FOLDER && !is_collapsed(k->path)) flatten(k, depth + 1);
    }
}

static void reload(void) {
    book_free(root);
    root = book_load(book_dir);
    nrows = 0;
    if (root) flatten(root, 0);
    if (page >= page_count()) page = page_count() - 1;
}

void book_screen_open(const char *dir) {
    if (strcmp(dir, book_dir) != 0) { page = 0; ncollapsed = 0; }
    snprintf(book_dir, sizeof book_dir, "%s", dir);
    snprintf(g_settings.last_book, sizeof g_settings.last_book, "%s", dir);
    g_settings.last_doc[0] = 0;
    app_save_settings();
    app_show(&screen_book);
}

static void enter(void) {
    reload();
}

static void draw_row(int i, const Row *row) {
    Rect r = row_rect(i);
    Node *n = row->node;
    int indent = g_margin / 2 + row->depth * (g_row_h * 6 / 10);
    int x = indent;
    int icon = g_row_h * 8 / 10;
    if (n->type == NODE_FOLDER) {
        draw_icon(is_collapsed(n->path) ? ICON_CHEVRON_RIGHT : ICON_CHEVRON_DOWN,
                  rect_make(x, r.y, icon * 7 / 10, g_row_h), DGRAY);
        x += icon * 7 / 10;
        draw_icon(ICON_FOLDER, rect_make(x, r.y + (g_row_h - icon) / 2, icon, icon), BLACK);
    } else {
        x += icon * 7 / 10;
        draw_icon(ICON_DOC, rect_make(x, r.y + (g_row_h - icon) / 2, icon, icon), DGRAY);
    }
    x += icon + 12;
    Rect text = rect_make(x, r.y, SW - x - g_row_h - g_margin, g_row_h);
    const char *title = n->title[0] ? n->title : n->name;
    if (n->type == NODE_FOLDER) {
        char buf[600];
        if (n->nkids == 0) snprintf(buf, sizeof buf, "%s  (%s)", title, T(S_EMPTY_FOLDER));
        else snprintf(buf, sizeof buf, "%s", title);
        draw_text_fit(f_ui_bold, BLACK, text, buf, 0);
    } else if (row->preview[0]) {
        draw_text_fit(f_ui, BLACK, text, row->preview, 0);
    } else {
        draw_text_fit(f_ui, BLACK, text, title, 0);
    }
    draw_icon(ICON_DOTS, dots_rect(i), DGRAY);
    FillArea(indent, r.y + r.h - 1, SW - indent - g_margin, 1, LGRAY);
}

static void draw(void) {
    draw_header(root ? root->title : "", ICON_BACK, 2);
    draw_icon(ICON_PLUS, header_slot(0), BLACK);
    draw_icon(ICON_BULB, header_slot(1), BLACK);
    int per = rows_per_page();
    for (int i = 0; i < per; i++) {
        int idx = page * per + i;
        if (idx >= nrows) break;
        /* A thicker rule above the special folders separates them from the manuscript. */
        if (rows[idx].depth == 0 && rows[idx].node->special && idx > 0 && !rows[idx - 1].node->special)
            FillArea(0, row_rect(i).y, SW, 3, BLACK);
        draw_row(i, &rows[idx]);
    }
    int fy = SH - footer_h();
    FillArea(0, fy, SW, 2, BLACK);
    if (page_count() > 1) {
        char buf[32];
        snprintf(buf, sizeof buf, "%d / %d", page + 1, page_count());
        draw_text_fit(f_ui, BLACK, rect_make(0, fy, SW, footer_h()), buf, 1);
        if (page > 0) draw_icon(ICON_LEFT, pager_prev(), BLACK);
        if (page + 1 < page_count()) draw_icon(ICON_RIGHT, pager_next(), BLACK);
    }
}

static void open_doc(const char *path, int show_keyboard) {
    editor_open(book_dir, path, -1, -1, show_keyboard);
}

static void after_change(int rc) {
    if (rc != 0) popup_message(T(S_OP_FAILED));
    reload();
    app_redraw(UPD_PARTIAL);
}

/* ---- adding things ---- */

typedef enum { ADD_NOTE_IN, ADD_FOLDER_IN } AddKind;
static AddKind add_kind;
static char add_parent[1024];

static void add_named(const char *title, void *ctx) {
    if (!title) return;
    reload();
    Node *parent = book_find_path(root, add_parent);
    char path[1024];
    NodeType type = add_kind == ADD_FOLDER_IN ? NODE_FOLDER : NODE_DOC;
    int rc = parent ? book_add(parent, type, title, path, sizeof path) : -1;
    if (rc != 0) { after_change(rc); return; }
    if (is_collapsed(parent->path)) toggle_collapsed(parent->path);
    if (type == NODE_DOC) open_doc(path, 1);
    else after_change(0);
}

static void start_add(AddKind kind, const char *parent_path, StrId title) {
    add_kind = kind;
    snprintf(add_parent, sizeof add_parent, "%s", parent_path);
    popup_prompt(T(title), "", add_named, NULL);
}

static void new_idea(void) {
    char path[1024];
    if (!root || book_new_idea(root, T(S_IDEAS), time(NULL), path, sizeof path) != 0) {
        popup_message(T(S_OP_FAILED));
        return;
    }
    open_doc(path, 1);
}

static void add_menu_chosen(int idx, void *ctx) {
    if (!root) return;
    if (idx == 0) start_add(ADD_NOTE_IN, root->path, S_NOTE_TITLE);
    else if (idx == 1) start_add(ADD_FOLDER_IN, root->path, S_FOLDER_TITLE);
    else if (idx == 2) new_idea();
}

/* ---- item menu ---- */

enum { M_OPEN, M_NEW_SCENE, M_NEW_SUB, M_NEW_NOTE, M_RENAME, M_UP, M_DOWN, M_DELETE };
static int menu_codes[8];

static void renamed(const char *title, void *ctx) {
    if (!title) return;
    reload();
    Node *n = book_find_path(root, menu_path);
    after_change(n ? book_rename(n, title, NULL, 0) : -1);
}

static void trash_confirmed(int ok, void *ctx) {
    if (!ok) return;
    reload();
    Node *n = book_find_path(root, menu_path);
    after_change(n ? book_trash(n, book_dir, time(NULL)) : -1);
}

static void item_menu_chosen(int idx, void *ctx) {
    if (idx < 0) return;
    Node *n = book_find_path(root, menu_path);
    if (!n) return;
    switch (menu_codes[idx]) {
    case M_OPEN: open_doc(n->path, 0); break;
    case M_NEW_SCENE: start_add(ADD_NOTE_IN, n->path, S_NOTE_TITLE); break;
    case M_NEW_SUB: start_add(ADD_FOLDER_IN, n->path, S_FOLDER_TITLE); break;
    case M_NEW_NOTE: start_add(ADD_NOTE_IN, n->path, S_NOTE_TITLE); break;
    case M_RENAME: popup_prompt(T(S_NEW_NAME), n->title, renamed, NULL); break;
    case M_UP: after_change(book_move(n, -1)); break;
    case M_DOWN: after_change(book_move(n, 1)); break;
    case M_DELETE: {
        char msg[800];
        snprintf(msg, sizeof msg, T(S_CONFIRM_TRASH), n->title);
        popup_confirm(msg, T(S_TRASH), trash_confirmed, NULL);
        break;
    }
    }
}

static void open_item_menu(Node *n) {
    snprintf(menu_path, sizeof menu_path, "%s", n->path);
    const char *items[8];
    int c = 0;
    if (n->type == NODE_DOC) {
        menu_codes[c] = M_OPEN; items[c++] = T(S_OPEN);
    } else if (n->special) {
        menu_codes[c] = M_NEW_NOTE; items[c++] = T(n->special == SPECIAL_IDEAS ? S_QUICK_NOTE : S_NEW_NOTE);
    } else {
        menu_codes[c] = M_NEW_SCENE; items[c++] = T(S_NEW_NOTE);
        menu_codes[c] = M_NEW_SUB; items[c++] = T(S_NEW_FOLDER);
    }
    if (!(n->special && n->type == NODE_FOLDER)) {
        menu_codes[c] = M_RENAME; items[c++] = T(S_RENAME);
        int in_special = n->parent && n->parent->special;
        if (!in_special && n->parent) {
            /* Offer only the moves that are possible among the regular (non-special) siblings. */
            int idx = -1, count = 0;
            for (int i = 0; i < n->parent->nkids; i++) {
                if (n->parent->kids[i]->special) continue;
                if (n->parent->kids[i] == n) idx = count;
                count++;
            }
            if (idx > 0) { menu_codes[c] = M_UP; items[c++] = T(S_MOVE_UP); }
            if (idx >= 0 && idx + 1 < count) { menu_codes[c] = M_DOWN; items[c++] = T(S_MOVE_DOWN); }
        }
        menu_codes[c] = M_DELETE; items[c++] = T(S_DELETE);
    }
    popup_menu(n->title, items, c, item_menu_chosen, NULL);
}

static void change_page(int delta) {
    int p = page + delta;
    if (p < 0 || p >= page_count()) return;
    page = p;
    app_redraw(UPD_PARTIAL);
}

static void go_back(void) {
    g_settings.last_book[0] = 0;
    app_save_settings();
    app_show(&screen_library);
}

static void pointer(int type, int x, int y) {
    if (type == EVT_POINTERDOWN) { down_y = y; return; }
    int per = rows_per_page();
    int row = (y - list_top()) / g_row_h;
    int idx = page * per + row;
    int on_row = y >= list_top() && y < SH - footer_h() && row < per && idx >= 0 && idx < nrows;
    if (type == EVT_POINTERLONG) {
        if (on_row) { down_y = -1; open_item_menu(rows[idx].node); }
        return;
    }
    if (type != EVT_POINTERUP || down_y < 0) return;
    int dy = y - down_y;
    down_y = -1;
    if (dy < -SH / 10) { change_page(1); return; }
    if (dy > SH / 10) { change_page(-1); return; }
    if (rect_hit(header_back_rect(), x, y)) { go_back(); return; }
    if (rect_hit(header_slot(0), x, y)) {
        const char *items[] = { T(S_NEW_NOTE), T(S_NEW_FOLDER), T(S_QUICK_NOTE) };
        popup_menu(T(S_ADD), items, 3, add_menu_chosen, NULL);
        return;
    }
    if (rect_hit(header_slot(1), x, y)) { new_idea(); return; }
    if (page_count() > 1 && rect_hit(pager_prev(), x, y)) { change_page(-1); return; }
    if (page_count() > 1 && rect_hit(pager_next(), x, y)) { change_page(1); return; }
    if (!on_row) return;
    Node *n = rows[idx].node;
    if (rect_hit(dots_rect(row), x, y)) { open_item_menu(n); return; }
    if (n->type == NODE_FOLDER) {
        toggle_collapsed(n->path);
        reload();
        app_redraw(UPD_PARTIAL);
    } else {
        open_doc(n->path, g_settings.keyboard_visible);
    }
}

static void device_key(int key) {
    if (key == IV_KEY_NEXT || key == IV_KEY_NEXT2 || key == IV_KEY_DOWN) change_page(1);
    else if (key == IV_KEY_PREV || key == IV_KEY_PREV2 || key == IV_KEY_UP) change_page(-1);
    else if (key == IV_KEY_BACK) go_back();
}

static void action(Action a) {
    if (a == ACT_PAGE_DOWN || a == ACT_DOWN) change_page(1);
    else if (a == ACT_PAGE_UP || a == ACT_UP) change_page(-1);
    else if (a == ACT_BACK) go_back();
}

Screen screen_book = { enter, draw, pointer, device_key, NULL, action, NULL };
