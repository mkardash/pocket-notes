/* File helpers: whole-file reads, safe saves, and history copies. */
#ifndef STORAGE_H
#define STORAGE_H
#include <stddef.h>
#include <time.h>

/* Reads the whole file. Returns malloc'd buffer (NUL-terminated) or NULL. */
char *storage_read(const char *path, int *len_out);
/* Writes to "<path>.tmp", flushes to disk, then renames over path. Returns 0 on success. */
int storage_write_atomic(const char *path, const char *data, int len);
/* Copies the current file into <book_dir>/.history/ with a timestamp and keeps the newest `keep`
 * copies for that document. Does nothing if the file is missing or empty. Returns 0 on success. */
int storage_backup(const char *book_dir, const char *doc_path, time_t now, int keep);

#endif
