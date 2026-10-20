#include "test.h"
#include "../src/core/book.h"
#include "../src/core/storage.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[512];

static void touch_file(const char *rel, const char *content) {
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    FILE *f = fopen(p, "wb");
    if (content) fputs(content, f);
    fclose(f);
}

static void mk(const char *rel) {
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    mkdir(p, 0755);
}

static int exists_rel(const char *rel) {
    char p[1024];
    struct stat st;
    snprintf(p, sizeof p, "%s/%s", root, rel);
    return stat(p, &st) == 0;
}

static int count_dir(const char *rel) {
    char p[1024];
    snprintf(p, sizeof p, "%s/%s", root, rel);
    DIR *d = opendir(p);
    if (!d) return -1;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d))) if (e->d_name[0] != '.' || strlen(e->d_name) > 2) {
        if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) n++;
    }
    closedir(d);
    return n;
}

static Node *load_book(void) {
    char p[1024];
    snprintf(p, sizeof p, "%s/Book", root);
    return book_load(p);
}

static void setup(void) {
    snprintf(root, sizeof root, "/tmp/pbw-test-XXXXXX");
    if (!mkdtemp(root)) { perror("mkdtemp"); exit(1); }
    mk("Book");
    mk("Book/02 Розділ 2");
    mk("Book/01 Розділ 1");
    mk("Book/Ідеї");
    mk("Book/Нотатки");
    mk("Book/.history");
    touch_file("Book/01 Розділ 1/02 Сцена 2.md", "two");
    touch_file("Book/01 Розділ 1/01 Сцена 1.md", "one");
    touch_file("Book/01 Розділ 1/cover.jpg", "x");
    touch_file("Book/02 Розділ 2/Чернетка.txt", "draft");
    touch_file("Book/Ідеї/2026-10-01 10-00.md", "idea");
}

static void teardown(void) {
    char cmd[600];
    snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
    if (system(cmd) != 0) fprintf(stderr, "cleanup failed\n");
}

static void test_parse_name(void) {
    int order;
    char t[256];
    book_parse_name("03 Сцена.md", 0, &order, t, sizeof t);
    CHECK_INT(order, 3);
    CHECK_STR(t, "Сцена");
    book_parse_name("12. Розділ", 1, &order, t, sizeof t);
    CHECK_INT(order, 12);
    CHECK_STR(t, "Розділ");
    book_parse_name("1984.md", 0, &order, t, sizeof t);  /* number without separator is a title */
    CHECK_INT(order, -1);
    CHECK_STR(t, "1984");
    book_parse_name("2026-10-20 23-06.md", 0, &order, t, sizeof t);  /* idea file */
    CHECK_INT(order, -1);
    CHECK_STR(t, "2026-10-20 23-06");
    book_parse_name("v1.2 notes", 1, &order, t, sizeof t);
    CHECK_INT(order, -1);
    CHECK_STR(t, "v1.2 notes");
}

static void test_clean_title(void) {
    char out[64];
    CHECK(book_clean_title("  Сцена: перша?  ", out, sizeof out));
    CHECK_STR(out, "Сцена- перша-");
    CHECK(!book_clean_title("   ...", out, sizeof out));
    /* Truncation never leaves half a Cyrillic letter. */
    CHECK(book_clean_title("абвгд", out, 6));
    CHECK_STR(out, "аб");
}

static void test_load_tree(void) {
    Node *b = load_book();
    CHECK(b != NULL);
    if (!b) return;
    CHECK_INT(b->nkids, 4);
    CHECK_STR(b->kids[0]->title, "Розділ 1");
    CHECK_STR(b->kids[1]->title, "Розділ 2");
    CHECK_STR(b->kids[2]->title, "Ідеї");
    CHECK_INT(b->kids[2]->special, SPECIAL_IDEAS);
    CHECK_INT(b->kids[3]->special, SPECIAL_NOTES);
    Node *ch1 = b->kids[0];
    CHECK_INT(ch1->nkids, 2);                    /* cover.jpg is ignored */
    CHECK_STR(ch1->kids[0]->title, "Сцена 1");
    CHECK_INT(ch1->kids[0]->type, NODE_DOC);
    CHECK_STR(b->kids[1]->kids[0]->title, "Чернетка");
    CHECK(book_find_path(b, ch1->kids[1]->path) == ch1->kids[1]);
    book_free(b);
}

static void test_add_rename_move(void) {
    Node *b = load_book();
    char path[1024];
    CHECK_INT(book_add(b->kids[0], NODE_DOC, "Нова сцена", path, sizeof path), 0);
    CHECK(exists_rel("Book/01 Розділ 1/03 Нова сцена.md"));
    CHECK_INT(book_add(b, NODE_FOLDER, "Розділ 3", path, sizeof path), 0);
    CHECK(exists_rel("Book/03 Розділ 3"));
    /* Adding into a folder with unnumbered items numbers them first. */
    CHECK_INT(book_add(b->kids[1], NODE_DOC, "Сцена", path, sizeof path), 0);
    CHECK(exists_rel("Book/02 Розділ 2/01 Чернетка.txt"));
    CHECK(exists_rel("Book/02 Розділ 2/02 Сцена.md"));
    /* Ideas keep plain names. */
    Node *ideas = book_special(b, SPECIAL_IDEAS);
    CHECK_INT(book_add(ideas, NODE_DOC, "Думка", path, sizeof path), 0);
    CHECK(exists_rel("Book/Ідеї/Думка.md"));
    book_free(b);

    b = load_book();
    Node *scene1 = b->kids[0]->kids[0];
    CHECK_INT(book_rename(scene1, "Початок", path, sizeof path), 0);
    CHECK(exists_rel("Book/01 Розділ 1/01 Початок.md"));
    book_free(b);

    b = load_book();
    /* Move "Нова сцена" (3rd) up to 2nd place. */
    CHECK_INT(book_move(b->kids[0]->kids[2], -1), 0);
    CHECK(exists_rel("Book/01 Розділ 1/02 Нова сцена.md"));
    CHECK(exists_rel("Book/01 Розділ 1/03 Сцена 2.md"));
    book_free(b);

    b = load_book();
    CHECK_INT(book_move(b->kids[0]->kids[0], -1), -1);   /* already first */
    CHECK_INT(book_move(b->kids[2], 1), -1);            /* Розділ 3 is the last regular item */
    CHECK_INT(book_move(b->kids[1], 1), 0);             /* Розділ 2 moves below Розділ 3 */
    book_free(b);
    b = load_book();
    CHECK_STR(b->kids[1]->title, "Розділ 3");
    CHECK_STR(b->kids[2]->title, "Розділ 2");
    CHECK_INT(b->kids[2]->order, 3);
    CHECK_INT(b->kids[2]->nkids, 2);                    /* folder moved with its scenes */
    CHECK_INT(b->kids[3]->special, SPECIAL_IDEAS);      /* special folders untouched */
    book_free(b);
}

static void test_move_same_titles(void) {
    mk("Book/09 Same");
    touch_file("Book/09 Same/01 Сцена.md", "first");
    touch_file("Book/09 Same/02 Сцена.md", "second");
    Node *b = load_book();
    Node *same = NULL;
    for (int i = 0; i < b->nkids; i++) if (!strcmp(b->kids[i]->title, "Same")) same = b->kids[i];
    CHECK(same != NULL);
    CHECK_INT(book_move(same->kids[1], -1), 0);
    book_free(b);
    char p[1024];
    snprintf(p, sizeof p, "%s/Book/09 Same/01 Сцена.md", root);
    char *data = storage_read(p, NULL);
    CHECK_STR(data, "second");
    free(data);
}

static void test_trash_and_idea(void) {
    Node *b = load_book();
    Node *victim = b->kids[0]->kids[0];
    CHECK_INT(book_trash(victim, b->path, 1760000000), 0);
    CHECK_INT(count_dir("Book/.trash"), 1);
    char path[1024];
    CHECK_INT(book_new_idea(b, "Ідеї", 1760000000, path, sizeof path), 0);
    CHECK(strstr(path, "/Ідеї/") != NULL);
    CHECK_INT(book_new_idea(b, "Ідеї", 1760000000, path, sizeof path), 0);
    CHECK(strstr(path, "(2).md") != NULL);
    book_free(b);
}

static void test_idea_creates_folder(void) {
    mk("Book2");
    char p[1024];
    snprintf(p, sizeof p, "%s/Book2", root);
    Node *b = book_load(p);
    char path[1024];
    CHECK_INT(book_new_idea(b, "Ідеї", 1760000000, path, sizeof path), 0);
    CHECK(exists_rel("Book2/Ідеї"));
    book_free(b);
}

static void test_library(void) {
    char path[1024];
    CHECK_INT(library_create_book(root, "Нова книга", "Розділ 1", "Сцена 1", "Ідеї", "Нотатки", path, sizeof path), 0);
    CHECK(exists_rel("Нова книга/01 Розділ 1/01 Сцена 1.md"));
    CHECK(exists_rel("Нова книга/Ідеї"));
    CHECK_INT(library_create_book(root, "Нова книга", "a", "b", "c", "d", path, sizeof path), -1);
    StrList l;
    CHECK_INT(library_list(root, &l), 0);
    CHECK_INT(l.n, 3);   /* Book, Book2, Нова книга */
    strlist_free(&l);
    CHECK_INT(library_rename_book(root, "Нова книга", "Інша: назва", path, sizeof path), 0);
    CHECK(exists_rel("Інша- назва/Ідеї"));
    CHECK_INT(library_rename_book(root, "Інша- назва", "Book", path, sizeof path), -1);  /* taken */
    CHECK_INT(library_create_empty(root, "Shopping", "Note", path, sizeof path), 0);
    CHECK(exists_rel("Shopping/01 Note.md"));
    CHECK_INT(library_create_empty(root, "Shopping", "Note", path, sizeof path), -1);
    CHECK_INT(library_trash_book(root, "Shopping", 1760000000), 0);
    CHECK_INT(library_trash_book(root, "Інша- назва", 1760000000), 0);
    CHECK(!exists_rel("Інша- назва"));
    CHECK_INT(count_dir(".trash"), 2);
}

static void test_storage(void) {
    char p[1024], book[1024];
    snprintf(book, sizeof book, "%s/Book", root);
    snprintf(p, sizeof p, "%s/Book/notes.md", root);
    CHECK_INT(storage_write_atomic(p, "привіт", (int)strlen("привіт")), 0);
    int len;
    char *data = storage_read(p, &len);
    CHECK_STR(data, "привіт");
    free(data);
    CHECK(!exists_rel("Book/notes.md.tmp"));
    for (int i = 0; i < 5; i++) CHECK_INT(storage_backup(book, p, 1760000000 + i, 3), 0);
    CHECK_INT(count_dir("Book/.history"), 3);
}

void test_book_all(void) {
    setup();
    RUN(test_parse_name);
    RUN(test_clean_title);
    RUN(test_load_tree);
    RUN(test_add_rename_move);
    RUN(test_move_same_titles);
    RUN(test_trash_and_idea);
    RUN(test_idea_creates_folder);
    RUN(test_library);
    RUN(test_storage);
    teardown();
}
