/* Text document stored as paragraphs (one per line of the file) with a cursor. */
#ifndef DOC_H
#define DOC_H

typedef struct {
    char *s;
    int len, cap;
    unsigned gen;       /* changes whenever the paragraph text changes (layout cache key) */
} Para;

typedef struct {
    Para *p;
    int n, cap;
    int cp, co;         /* cursor: paragraph index and byte offset inside it */
    int modified;       /* unsaved changes */
    unsigned next_gen;
} Doc;

/* Builds a document from file contents. Accepts \n, \r\n and \r; drops a UTF-8 BOM. */
Doc *doc_from_text(const char *text, int len);
void doc_free(Doc *d);
/* Joins paragraphs with \n. Caller frees. */
char *doc_text(const Doc *d, int *len_out);

/* Inserts UTF-8 text at the cursor; '\n' splits paragraphs. Cursor ends after the text. */
void doc_insert(Doc *d, const char *s, int len);
/* Deletes the character before the cursor (joins paragraphs at a paragraph start). */
void doc_backspace(Doc *d);
/* Deletes the character after the cursor (joins with the next paragraph at the end). */
void doc_delete_forward(Doc *d);
void doc_left(Doc *d);
void doc_right(Doc *d);
/* Moves the cursor, clamped to valid positions and character boundaries. */
void doc_set_cursor(Doc *d, int para, int off);
/* Replaces the text of paragraph i. */
void doc_set_para(Doc *d, int i, const char *s, int len);
/* Inserts a paragraph before index i (i == n appends); the cursor stays on its paragraph. */
void doc_insert_para(Doc *d, int i, const char *s, int len);
/* Removes paragraph i (no-op for the last one left); the cursor stays valid. */
void doc_delete_para(Doc *d, int i);
int doc_word_count(const Doc *d);

#endif
