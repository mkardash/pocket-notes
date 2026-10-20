#include "book.h"
#include "utf8.h"
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = malloc(n);
    memcpy(d, s, n);
    return d;
}

static int ends_with_ci(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    if (a < b) return 0;
    for (size_t i = 0; i < b; i++) {
        char x = s[a - b + i], y = suffix[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (x != y) return 0;
    }
    return 1;
}

int book_is_doc_name(const char *name) {
    return ends_with_ci(name, ".md") || ends_with_ci(name, ".txt") || ends_with_ci(name, ".markdown");
}

static void strip_ext(char *s) {
    char *dot = strrchr(s, '.');
    if (dot && dot != s) *dot = 0;
}

void book_parse_name(const char *name, int is_dir, int *order, char *title, size_t cap) {
    char base[512];
    snprintf(base, sizeof base, "%s", name);
    if (!is_dir) strip_ext(base);
    int digits = 0, value = 0;
    while (base[digits] >= '0' && base[digits] <= '9' && digits < 6) {
        value = value * 10 + (base[digits] - '0');
        digits++;
    }
    const char *rest = base;
    *order = -1;
    /* 1-3 digits only: "2026-10-20 ..." (idea files) and "1984" are titles, not positions. */
    if (digits > 0 && digits <= 3) {
        const char *q = base + digits;
        if (*q == ' ' || *q == '.' || *q == '-' || *q == '_') {
            while (*q == ' ' || *q == '.' || *q == '-' || *q == '_') q++;
            if (*q) { *order = value; rest = q; }
        }
    }
    snprintf(title, cap, "%s", rest);
}

int book_clean_title(const char *in, char *out, size_t cap) {
    size_t o = 0;
    for (const char *p = in; *p && o + 1 < cap; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 32 || strchr("\\/:*?\"<>|", c)) c = '-';
        out[o++] = (char)c;
    }
    out[o] = 0;
    /* Trim leading spaces and trailing spaces/dots. */
    size_t start = 0;
    while (out[start] == ' ') start++;
    if (start) memmove(out, out + start, strlen(out + start) + 1);
    size_t n = strlen(out);
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.')) out[--n] = 0;
    /* Never leave half a UTF-8 character at the end (snprintf-style truncation can cut one). */
    size_t good = 0, pos = 0;
    while (pos < n) {
        uint32_t cp;
        int step = utf8_decode(out + pos, (int)(n - pos), &cp);
        if (cp == 0xFFFD && step == 1) break;
        pos += step;
        good = pos;
    }
    out[good] = 0;
    n = good;
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '.')) out[--n] = 0;
    return n > 0;
}

static int lower_equals(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b), i = 0, j = 0;
    while (i < la && j < lb) {
        uint32_t x, y;
        i += utf8_decode(a + i, la - i, &x);
        j += utf8_decode(b + j, lb - j, &y);
        if (utf8_tolower(x) != utf8_tolower(y)) return 0;
    }
    return i == la && j == lb;
}

static int special_kind(const char *title) {
    static const char *ideas[] = { "Ідеї", "Ideas", "Scratchpad", NULL };
    static const char *notes[] = { "Нотатки", "Notes", "Project Notes", NULL };
    for (int i = 0; ideas[i]; i++) if (lower_equals(title, ideas[i])) return SPECIAL_IDEAS;
    for (int i = 0; notes[i]; i++) if (lower_equals(title, notes[i])) return SPECIAL_NOTES;
    return SPECIAL_NONE;
}

static int node_cmp(const void *pa, const void *pb) {
    const Node *a = *(Node *const *)pa, *b = *(Node *const *)pb;
    if (a->special != b->special) {
        if (!a->special) return -1;
        if (!b->special) return 1;
        return a->special - b->special;
    }
    if ((a->order >= 0) != (b->order >= 0)) return a->order >= 0 ? -1 : 1;
    if (a->order != b->order) return a->order - b->order;
    return strcmp(a->title, b->title);
}

static Node *new_node(NodeType type, const char *name, const char *path, Node *parent) {
    Node *n = calloc(1, sizeof(Node));
    char title[512];
    n->type = type;
    n->name = xstrdup(name);
    n->path = xstrdup(path);
    n->parent = parent;
    book_parse_name(name, type == NODE_FOLDER, &n->order, title, sizeof title);
    n->title = xstrdup(title);
    return n;
}

static void load_children(Node *dirnode, int depth) {
    DIR *d = opendir(dirnode->path);
    if (!d) return;
    struct dirent *e;
    int cap = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[BOOK_PATH_MAX];
        snprintf(path, sizeof path, "%s/%s", dirnode->path, e->d_name);
        struct stat st;
        if (stat(path, &st) != 0) continue;
        NodeType type;
        if (S_ISDIR(st.st_mode)) type = NODE_FOLDER;
        else if (S_ISREG(st.st_mode) && book_is_doc_name(e->d_name)) type = NODE_DOC;
        else continue;
        Node *k = new_node(type, e->d_name, path, dirnode);
        if (depth == 0 && type == NODE_FOLDER) k->special = special_kind(k->title);
        if (dirnode->nkids == cap) {
            cap = cap ? cap * 2 : 8;
            dirnode->kids = realloc(dirnode->kids, sizeof(Node *) * cap);
        }
        dirnode->kids[dirnode->nkids++] = k;
        if (type == NODE_FOLDER && depth < 16) load_children(k, depth + 1);
    }
    closedir(d);
    if (dirnode->nkids > 1) qsort(dirnode->kids, dirnode->nkids, sizeof(Node *), node_cmp);
}

Node *book_load(const char *dir) {
    struct stat st;
    if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) return NULL;
    const char *base = strrchr(dir, '/');
    base = base ? base + 1 : dir;
    Node *root = new_node(NODE_FOLDER, base, dir, NULL);
    free(root->title);
    root->title = xstrdup(base);
    root->order = -1;
    load_children(root, 0);
    return root;
}

void book_free(Node *n) {
    if (!n) return;
    for (int i = 0; i < n->nkids; i++) book_free(n->kids[i]);
    free(n->kids);
    free(n->name);
    free(n->title);
    free(n->path);
    free(n);
}

Node *book_find_path(Node *root, const char *path) {
    if (!root) return NULL;
    if (strcmp(root->path, path) == 0) return root;
    for (int i = 0; i < root->nkids; i++) {
        Node *f = book_find_path(root->kids[i], path);
        if (f) return f;
    }
    return NULL;
}

Node *book_special(Node *root, int special) {
    for (int i = 0; root && i < root->nkids; i++)
        if (root->kids[i]->special == special) return root->kids[i];
    return NULL;
}

static int exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

static void dir_of(const Node *n, char *out, size_t cap) {
    snprintf(out, cap, "%s", n->path);
    char *slash = strrchr(out, '/');
    if (slash) *slash = 0;
}

static void make_name(char *out, size_t cap, int order, int width, const char *title, NodeType type) {
    const char *ext = type == NODE_DOC ? ".md" : "";
    if (order >= 0) snprintf(out, cap, "%0*d %s%s", width, order, title, ext);
    else snprintf(out, cap, "%s%s", title, ext);
}

/* Keeps the original extension (.txt etc.) for documents when renaming. */
static void make_name_like(char *out, size_t cap, const Node *n, int order, int width, const char *title) {
    if (n->type == NODE_DOC) {
        const char *dot = strrchr(n->name, '.');
        if (order >= 0) snprintf(out, cap, "%0*d %s%s", width, order, title, dot ? dot : ".md");
        else snprintf(out, cap, "%s%s", title, dot ? dot : ".md");
    } else {
        make_name(out, cap, order, width, title, NODE_FOLDER);
    }
}

static int width_for(int count) {
    int w = 2;
    for (int c = 100; count >= c; c *= 10) w++;
    return w;
}

/* Gives every non-special child a sequential prefix in the given order.
 * Renames in two steps through temporary names so swaps can't collide. */
static int renumber(Node *parent, Node **ordered, int count) {
    int width = width_for(count);
    char dir[BOOK_PATH_MAX];
    snprintf(dir, sizeof dir, "%s", parent->path);
    char (*finals)[BOOK_PATH_MAX] = malloc(sizeof(*finals) * (count ? count : 1));
    char (*temps)[BOOK_PATH_MAX] = malloc(sizeof(*temps) * (count ? count : 1));
    int *changed = calloc(count ? count : 1, sizeof(int));
    int rc = 0;
    for (int i = 0; i < count; i++) {
        char name[600];
        make_name_like(name, sizeof name, ordered[i], i + 1, width, ordered[i]->title);
        snprintf(finals[i], BOOK_PATH_MAX, "%s/%s", dir, name);
        changed[i] = strcmp(finals[i], ordered[i]->path) != 0;
    }
    for (int i = 0; i < count && rc == 0; i++) {
        if (!changed[i]) continue;
        snprintf(temps[i], BOOK_PATH_MAX, "%s/.renumber-%d-%ld", dir, i, (long)getpid());
        if (rename(ordered[i]->path, temps[i]) != 0) { rc = -1; changed[i] = 0; }
    }
    for (int i = 0; i < count; i++) {
        if (!changed[i]) continue;
        if (rc == 0 && exists(finals[i])) rc = -1;
        /* On any failure, put the item back under its old name. */
        const char *target = rc == 0 ? finals[i] : ordered[i]->path;
        if (rename(temps[i], target) != 0) rc = -1;
    }
    free(finals);
    free(temps);
    free(changed);
    return rc;
}

/* Non-special children of parent in display order. Caller frees. */
static Node **regular_kids(Node *parent, int *count) {
    Node **list = malloc(sizeof(Node *) * (parent->nkids + 1));
    int c = 0;
    for (int i = 0; i < parent->nkids; i++)
        if (!parent->kids[i]->special) list[c++] = parent->kids[i];
    *count = c;
    return list;
}

int book_add(Node *parent, NodeType type, const char *title, char *out_path, size_t cap) {
    char clean[400];
    if (!parent || parent->type != NODE_FOLDER || !book_clean_title(title, clean, sizeof clean)) return -1;
    int count;
    Node **kids = regular_kids(parent, &count);
    int needs_renumber = 0, max_order = 0;
    for (int i = 0; i < count; i++) {
        if (kids[i]->order < 0) needs_renumber = 1;
        if (kids[i]->order > max_order) max_order = kids[i]->order;
    }
    int order;
    if (parent->special == SPECIAL_IDEAS || parent->special == SPECIAL_NOTES) {
        order = -1; /* notes and ideas keep plain names */
        needs_renumber = 0;
    } else if (needs_renumber) {
        if (renumber(parent, kids, count) != 0) { free(kids); return -1; }
        order = count + 1;
    } else {
        order = max_order + 1;
    }
    free(kids);
    char name[600], path[BOOK_PATH_MAX];
    make_name(name, sizeof name, order, width_for(order), clean, type);
    snprintf(path, sizeof path, "%s/%s", parent->path, name);
    if (exists(path)) return -1;
    if (type == NODE_FOLDER) {
        if (mkdir(path, 0755) != 0) return -1;
    } else {
        FILE *f = fopen(path, "wb");
        if (!f) return -1;
        fclose(f);
    }
    if (out_path) snprintf(out_path, cap, "%s", path);
    return 0;
}

int book_rename(Node *n, const char *title, char *out_path, size_t cap) {
    char clean[400], dir[BOOK_PATH_MAX], name[600], path[BOOK_PATH_MAX];
    if (!n || !n->parent || !book_clean_title(title, clean, sizeof clean)) return -1;
    dir_of(n, dir, sizeof dir);
    int width = 2;
    if (n->order >= 0) {
        /* Keep the existing prefix width, e.g. "007". */
        int digits = 0;
        while (n->name[digits] >= '0' && n->name[digits] <= '9') digits++;
        width = digits;
    }
    make_name_like(name, sizeof name, n, n->order, width, clean);
    snprintf(path, sizeof path, "%s/%s", dir, name);
    if (strcmp(path, n->path) != 0) {
        /* Allow case-only renames on case-insensitive file systems. */
        if (exists(path) && strcasecmp(path, n->path) != 0) return -1;
        if (rename(n->path, path) != 0) return -1;
    }
    if (out_path) snprintf(out_path, cap, "%s", path);
    return 0;
}

int book_move(Node *n, int delta) {
    if (!n || !n->parent || n->special) return -1;
    int count;
    Node **kids = regular_kids(n->parent, &count);
    int idx = -1;
    for (int i = 0; i < count; i++) if (kids[i] == n) idx = i;
    int target = idx + delta;
    if (idx < 0 || target < 0 || target >= count) { free(kids); return -1; }
    Node *tmp = kids[idx];
    kids[idx] = kids[target];
    kids[target] = tmp;
    int rc = renumber(n->parent, kids, count);
    free(kids);
    return rc;
}

static void stamp(time_t now, char *out, size_t cap, const char *fmt) {
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(out, cap, fmt, &tmv);
}

int book_trash(Node *n, const char *book_dir, time_t now) {
    if (!n || !n->parent) return -1;
    char trash[BOOK_PATH_MAX], st[32], target[BOOK_PATH_MAX];
    snprintf(trash, sizeof trash, "%s/.trash", book_dir);
    mkdir(trash, 0755);
    stamp(now, st, sizeof st, "%Y%m%d-%H%M%S");
    for (int i = 1; i < 100; i++) {
        if (i == 1) snprintf(target, sizeof target, "%s/%s %s", trash, st, n->name);
        else snprintf(target, sizeof target, "%s/%s-%d %s", trash, st, i, n->name);
        if (!exists(target)) return rename(n->path, target) == 0 ? 0 : -1;
    }
    return -1;
}

int book_new_idea(Node *root, const char *ideas_title, time_t now, char *out_path, size_t cap) {
    char dir[BOOK_PATH_MAX], st[32], path[BOOK_PATH_MAX];
    Node *ideas = book_special(root, SPECIAL_IDEAS);
    if (ideas) snprintf(dir, sizeof dir, "%s", ideas->path);
    else {
        snprintf(dir, sizeof dir, "%s/%s", root->path, ideas_title);
        if (mkdir(dir, 0755) != 0 && errno != EEXIST) return -1;
    }
    stamp(now, st, sizeof st, "%Y-%m-%d %H-%M");
    for (int i = 1; i < 100; i++) {
        if (i == 1) snprintf(path, sizeof path, "%s/%s.md", dir, st);
        else snprintf(path, sizeof path, "%s/%s (%d).md", dir, st, i);
        if (exists(path)) continue;
        FILE *f = fopen(path, "wb");
        if (!f) return -1;
        fclose(f);
        if (out_path) snprintf(out_path, cap, "%s", path);
        return 0;
    }
    return -1;
}

static int str_cmp(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

int library_list(const char *root, StrList *out) {
    out->items = NULL;
    out->n = 0;
    DIR *d = opendir(root);
    if (!d) return -1;
    struct dirent *e;
    int cap = 0;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[BOOK_PATH_MAX];
        struct stat st;
        snprintf(path, sizeof path, "%s/%s", root, e->d_name);
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        if (out->n == cap) {
            cap = cap ? cap * 2 : 8;
            out->items = realloc(out->items, sizeof(char *) * cap);
        }
        out->items[out->n++] = xstrdup(e->d_name);
    }
    closedir(d);
    if (out->n > 1) qsort(out->items, out->n, sizeof(char *), str_cmp);
    return 0;
}

void strlist_free(StrList *l) {
    for (int i = 0; i < l->n; i++) free(l->items[i]);
    free(l->items);
    l->items = NULL;
    l->n = 0;
}

int library_create_book(const char *root, const char *title, const char *chapter, const char *scene,
                        const char *ideas, const char *notes, char *out_path, size_t cap) {
    char clean[400], book[BOOK_PATH_MAX], p[BOOK_PATH_MAX];
    if (!book_clean_title(title, clean, sizeof clean)) return -1;
    mkdir(root, 0755);
    snprintf(book, sizeof book, "%s/%s", root, clean);
    if (exists(book) || mkdir(book, 0755) != 0) return -1;
    snprintf(p, sizeof p, "%s/01 %s", book, chapter);
    if (mkdir(p, 0755) == 0) {
        snprintf(p, sizeof p, "%s/01 %s/01 %s.md", book, chapter, scene);
        FILE *f = fopen(p, "wb");
        if (f) fclose(f);
    }
    snprintf(p, sizeof p, "%s/%s", book, ideas);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/%s", book, notes);
    mkdir(p, 0755);
    if (out_path) snprintf(out_path, cap, "%s", book);
    return 0;
}

int library_rename_book(const char *root, const char *name, const char *new_title, char *out_path, size_t cap) {
    char clean[400], from[BOOK_PATH_MAX], to[BOOK_PATH_MAX];
    if (!book_clean_title(new_title, clean, sizeof clean)) return -1;
    snprintf(from, sizeof from, "%s/%s", root, name);
    snprintf(to, sizeof to, "%s/%s", root, clean);
    if (strcmp(from, to) != 0) {
        if (exists(to) && strcasecmp(from, to) != 0) return -1;
        if (rename(from, to) != 0) return -1;
    }
    if (out_path) snprintf(out_path, cap, "%s", to);
    return 0;
}

int library_trash_book(const char *root, const char *name, time_t now) {
    char trash[BOOK_PATH_MAX], st[32], from[BOOK_PATH_MAX], target[BOOK_PATH_MAX];
    snprintf(from, sizeof from, "%s/%s", root, name);
    if (!exists(from)) return -1;
    snprintf(trash, sizeof trash, "%s/.trash", root);
    mkdir(trash, 0755);
    stamp(now, st, sizeof st, "%Y%m%d-%H%M%S");
    for (int i = 1; i < 100; i++) {
        if (i == 1) snprintf(target, sizeof target, "%s/%s %s", trash, st, name);
        else snprintf(target, sizeof target, "%s/%s-%d %s", trash, st, i, name);
        if (!exists(target)) return rename(from, target) == 0 ? 0 : -1;
    }
    return -1;
}

int library_create_empty(const char *root, const char *title, const char *note, char *out_path, size_t cap) {
    char clean[400], dir[BOOK_PATH_MAX], p[BOOK_PATH_MAX];
    if (!book_clean_title(title, clean, sizeof clean)) return -1;
    mkdir(root, 0755);
    snprintf(dir, sizeof dir, "%s/%s", root, clean);
    if (exists(dir) || mkdir(dir, 0755) != 0) return -1;
    snprintf(p, sizeof p, "%s/01 %s.md", dir, note);
    FILE *f = fopen(p, "wb");
    if (f) fclose(f);
    if (out_path) snprintf(out_path, cap, "%s", dir);
    return 0;
}
