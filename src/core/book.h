/* A project is a folder. Sub-folders (any depth) hold notes, which are .md files.
 * Order comes from a number prefix in the name: "01 Chapter 1/02 Scene.md".
 * Special folders at the project root ("Ideas"/"Ідеї", "Notes"/"Нотатки") sort last.
 * (Function names still say "book": a book is one kind of project.) */
#ifndef BOOK_H
#define BOOK_H
#include <stddef.h>
#include <time.h>

typedef enum { NODE_FOLDER = 0, NODE_DOC = 1 } NodeType;
enum { SPECIAL_NONE = 0, SPECIAL_IDEAS = 1, SPECIAL_NOTES = 2 };

typedef struct Node {
    NodeType type;
    int special;
    int order;              /* number prefix, -1 when the name has none */
    char *name;             /* name on disk */
    char *title;            /* name without prefix and extension */
    char *path;             /* full path */
    struct Node *parent;
    struct Node **kids;
    int nkids;
} Node;

#define BOOK_PATH_MAX 1024

/* "03 Title.md" -> order 3, title "Title". The number has 1-3 digits; separators: space . - _ */
void book_parse_name(const char *name, int is_dir, int *order, char *title, size_t cap);
/* Replaces characters FAT/exFAT can't store and trims spaces/dots. Returns 0 if nothing is left. */
int book_clean_title(const char *in, char *out, size_t cap);
int book_is_doc_name(const char *name);

/* Loads the folder tree under dir. Hidden entries (starting with '.') are skipped. */
Node *book_load(const char *dir);
void book_free(Node *n);
Node *book_find_path(Node *root, const char *path);
Node *book_special(Node *root, int special);

/* All operations work on disk; reload the tree afterwards. Return 0 on success, -1 on error. */
int book_add(Node *parent, NodeType type, const char *title, char *out_path, size_t cap);
int book_rename(Node *n, const char *title, char *out_path, size_t cap);
int book_move(Node *n, int delta);
int book_trash(Node *n, const char *book_dir, time_t now);
/* Creates a new empty idea file (named by date/time) in the Ideas folder, creating it if needed. */
int book_new_idea(Node *root, const char *ideas_title, time_t now, char *out_path, size_t cap);

/* Library: every non-hidden folder in root is a book. */
typedef struct { char **items; int n; } StrList;
int library_list(const char *root, StrList *out);
void strlist_free(StrList *l);
/* Renames a book folder. Returns 0 on success. */
int library_rename_book(const char *root, const char *name, const char *new_title, char *out_path, size_t cap);
/* Moves a book folder into <root>/.trash/. Returns 0 on success. */
int library_trash_book(const char *root, const char *name, time_t now);
/* Project templates. Book: "01 <chapter>/01 <scene>.md" plus ideas and notes folders. */
int library_create_book(const char *root, const char *title, const char *chapter, const char *scene,
                        const char *ideas, const char *notes, char *out_path, size_t cap);
/* Empty project with one note "01 <note>.md". */
int library_create_empty(const char *root, const char *title, const char *note, char *out_path, size_t cap);

#endif
