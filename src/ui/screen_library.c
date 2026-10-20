/* "My projects": every folder in the PocketNotes folder is a project. */
#include "app.h"
#include "../core/book.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static StrList books;          /* project folder names */
static int template_choice = 0;  /* 0 empty, 1 book */
static int page = 0;
static int down_y = -1, down_x = -1;
static int menu_book = -1;

static int list_top(void) { return g_header_h; }
static int footer_h(void) { return g_row_h; }
static int rows_per_page(void) { return (SH - list_top() - footer_h()) / g_row_h; }
static int page_count(void) { int r = rows_per_page(); return books.n ? (books.n + r - 1) / r : 1; }
static Rect row_rect(int i) { return rect_make(0, list_top() + i * g_row_h, SW, g_row_h); }
static Rect dots_rect(int i) { Rect r = row_rect(i); return rect_make(SW - g_row_h - g_margin / 2, r.y, g_row_h, g_row_h); }
static Rect empty_button(void) { return rect_make(SW / 4, SH / 2, SW / 2, g_row_h); }
static Rect pager_prev(void) { return rect_make(0, SH - footer_h(), SW / 4, footer_h()); }
static Rect pager_next(void) { return rect_make(SW * 3 / 4, SH - footer_h(), SW / 4, footer_h()); }

static void reload(void) {
    strlist_free(&books);
    library_list(g_root, &books);
    if (page >= page_count()) page = page_count() - 1;
}

static void enter(void) {
    reload();
}

static void draw(void) {
    draw_header(T(S_MY_PROJECTS), ICON_CLOSE);
    draw_icon(ICON_PLUS, header_slot(0), BLACK);
    if (books.n == 0) {
        draw_wrapped(f_ui, DGRAY, rect_make(g_margin * 2, SH / 3, SW - 4 * g_margin, SH / 6), T(S_NO_PROJECTS), 1);
        draw_button(empty_button(), T(S_NEW_PROJECT), 1);
    }
    int per = rows_per_page();
    for (int i = 0; i < per; i++) {
        int idx = page * per + i;
        if (idx >= books.n) break;
        Rect r = row_rect(i);
        draw_icon(ICON_BOOK, rect_make(g_margin / 2, r.y, g_row_h, g_row_h), BLACK);
        draw_text_fit(f_ui, BLACK, rect_make(g_margin / 2 + g_row_h + 10, r.y, SW - 2 * g_row_h - 2 * g_margin, g_row_h),
                      books.items[idx], 0);
        draw_icon(ICON_DOTS, dots_rect(i), DGRAY);
        FillArea(g_margin, r.y + r.h - 1, SW - 2 * g_margin, 1, LGRAY);
    }
    /* Footer: pager or storage hint. */
    int fy = SH - footer_h();
    FillArea(0, fy, SW, 2, BLACK);
    if (page_count() > 1) {
        char buf[32];
        snprintf(buf, sizeof buf, "%d / %d", page + 1, page_count());
        draw_text_fit(f_ui, BLACK, rect_make(0, fy, SW, footer_h()), buf, 1);
        if (page > 0) draw_icon(ICON_LEFT, pager_prev(), BLACK);
        if (page + 1 < page_count()) draw_icon(ICON_RIGHT, pager_next(), BLACK);
    } else {
        draw_text_fit(f_small, DGRAY, rect_make(g_margin, fy, SW - 2 * g_margin, footer_h()), T(S_STORAGE_HINT), 1);
    }
}

static void created(const char *title, void *ctx) {
    if (!title) return;
    char path[1024];
    int rc = template_choice == 1
        ? library_create_book(g_root, title, T(S_DEFAULT_CHAPTER), T(S_DEFAULT_SCENE), T(S_IDEAS), T(S_NOTES),
                              path, sizeof path)
        : library_create_empty(g_root, title, T(S_DEFAULT_NOTE), path, sizeof path);
    if (rc != 0) {
        popup_message(T(S_OP_FAILED));
        return;
    }
    book_screen_open(path);
}

static void renamed(const char *title, void *ctx) {
    if (!title || menu_book < 0 || menu_book >= books.n) return;
    char old_path[1024], new_path[1024];
    snprintf(old_path, sizeof old_path, "%s/%s", g_root, books.items[menu_book]);
    if (library_rename_book(g_root, books.items[menu_book], title, new_path, sizeof new_path) != 0) {
        popup_message(T(S_OP_FAILED));
        return;
    }
    if (!strcmp(g_settings.last_book, old_path)) {
        snprintf(g_settings.last_book, sizeof g_settings.last_book, "%s", new_path);
        g_settings.last_doc[0] = 0;
        app_save_settings();
    }
    reload();
    app_redraw(UPD_PARTIAL);
}

static void trash_confirmed(int ok, void *ctx) {
    if (!ok || menu_book < 0 || menu_book >= books.n) return;
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", g_root, books.items[menu_book]);
    if (library_trash_book(g_root, books.items[menu_book], time(NULL)) != 0) {
        popup_message(T(S_OP_FAILED));
        return;
    }
    if (!strcmp(g_settings.last_book, path)) {
        g_settings.last_book[0] = g_settings.last_doc[0] = 0;
        app_save_settings();
    }
    reload();
    app_redraw(UPD_PARTIAL);
}

static void book_menu_chosen(int idx, void *ctx) {
    if (menu_book < 0 || menu_book >= books.n) return;
    if (idx == 0) {
        popup_prompt(T(S_NEW_NAME), books.items[menu_book], renamed, NULL);
    } else if (idx == 1) {
        char msg[700];
        snprintf(msg, sizeof msg, T(S_CONFIRM_TRASH), books.items[menu_book]);
        popup_confirm(msg, T(S_TRASH), trash_confirmed, NULL);
    }
}

static void open_book_menu(int idx) {
    menu_book = idx;
    const char *items[] = { T(S_RENAME), T(S_DELETE) };
    popup_menu(books.items[idx], items, 2, book_menu_chosen, NULL);
}

static void template_chosen(int idx, void *ctx) {
    if (idx < 0) return;
    template_choice = idx;
    popup_prompt(T(S_PROJECT_TITLE), "", created, NULL);
}

static void new_project(void) {
    const char *items[] = { T(S_TEMPLATE_EMPTY), T(S_TEMPLATE_BOOK) };
    popup_menu(T(S_NEW_PROJECT), items, 2, template_chosen, NULL);
}

static void change_page(int delta) {
    int p = page + delta;
    if (p < 0 || p >= page_count()) return;
    page = p;
    app_redraw(UPD_PARTIAL);
}

static void pointer(int type, int x, int y) {
    if (type == EVT_POINTERDOWN) { down_x = x; down_y = y; return; }
    int per = rows_per_page();
    int row = (y - list_top()) / g_row_h;
    int idx = page * per + row;
    int on_row = y >= list_top() && y < SH - footer_h() && idx >= 0 && idx < books.n && row < per;
    if (type == EVT_POINTERLONG) {
        if (on_row) { down_y = -1; open_book_menu(idx); }
        return;
    }
    if (type != EVT_POINTERUP || down_y < 0) return;
    int dy = y - down_y;
    down_y = -1;
    if (dy < -SH / 10) { change_page(1); return; }
    if (dy > SH / 10) { change_page(-1); return; }
    if (rect_hit(header_back_rect(), x, y)) { app_exit(); return; }
    if (rect_hit(header_slot(0), x, y) || (books.n == 0 && rect_hit(empty_button(), x, y))) {
        new_project();
        return;
    }
    if (page_count() > 1 && rect_hit(pager_prev(), x, y)) { change_page(-1); return; }
    if (page_count() > 1 && rect_hit(pager_next(), x, y)) { change_page(1); return; }
    if (on_row) {
        if (rect_hit(dots_rect(row), x, y)) open_book_menu(idx);
        else {
            char path[1024];
            snprintf(path, sizeof path, "%s/%s", g_root, books.items[idx]);
            book_screen_open(path);
        }
    }
}

static void device_key(int key) {
    if (key == IV_KEY_NEXT || key == IV_KEY_NEXT2 || key == IV_KEY_DOWN) change_page(1);
    else if (key == IV_KEY_PREV || key == IV_KEY_PREV2 || key == IV_KEY_UP) change_page(-1);
    else if (key == IV_KEY_BACK || key == IV_KEY_HOME) app_exit();
}

static void action(Action a) {
    if (a == ACT_PAGE_DOWN || a == ACT_DOWN) change_page(1);
    else if (a == ACT_PAGE_UP || a == ACT_UP) change_page(-1);
}

Screen screen_library = { enter, draw, pointer, device_key, NULL, action, NULL };
