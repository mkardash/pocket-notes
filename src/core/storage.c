#include "storage.h"
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

char *storage_read(const char *path, int *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long size = ftell(f);
    if (size < 0) { fclose(f); return NULL; }
    rewind(f);
    char *buf = malloc(size + 1);
    size_t got = fread(buf, 1, size, f);
    fclose(f);
    if ((long)got != size) { free(buf); return NULL; }
    buf[size] = 0;
    if (len_out) *len_out = (int)size;
    return buf;
}

int storage_write_atomic(const char *path, const char *data, int len) {
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;
    int off = 0;
    while (off < len) {
        ssize_t w = write(fd, data + off, len - off);
        if (w <= 0) { close(fd); unlink(tmp); return -1; }
        off += (int)w;
    }
    if (fsync(fd) != 0 || close(fd) != 0) { unlink(tmp); return -1; }
    if (rename(tmp, path) != 0) { unlink(tmp); return -1; }
    return 0;
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }

/* History name: "<parent folder> - <file name without extension> <timestamp>.md" */
static void history_base(const char *doc_path, char *out, size_t cap) {
    char tmp[1024];
    snprintf(tmp, sizeof tmp, "%s", doc_path);
    char *file = strrchr(tmp, '/');
    const char *parent = "";
    if (file) {
        *file++ = 0;
        char *p = strrchr(tmp, '/');
        parent = p ? p + 1 : tmp;
    } else {
        file = tmp;
    }
    char *dot = strrchr(file, '.');
    if (dot && dot != file) *dot = 0;
    snprintf(out, cap, "%s - %s ", parent, file);
}

int storage_backup(const char *book_dir, const char *doc_path, time_t now, int keep) {
    int len = 0;
    char *data = storage_read(doc_path, &len);
    if (!data) return 0;
    if (len == 0) { free(data); return 0; }
    char dir[1100], base[600], st[32], path[1800];
    snprintf(dir, sizeof dir, "%s/.history", book_dir);
    mkdir(dir, 0755);
    history_base(doc_path, base, sizeof base);
    struct tm tmv;
    localtime_r(&now, &tmv);
    strftime(st, sizeof st, "%Y%m%d-%H%M%S", &tmv);
    snprintf(path, sizeof path, "%s/%s%s.md", dir, base, st);
    int rc = storage_write_atomic(path, data, len);
    free(data);
    if (rc != 0) return rc;

    /* Prune: keep the newest `keep` copies with this base name. Timestamps sort by name. */
    DIR *d = opendir(dir);
    if (!d) return 0;
    char **names = NULL;
    int n = 0, cap = 0;
    size_t blen = strlen(base);
    struct dirent *e;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, base, blen) != 0) continue;
        if (n == cap) { cap = cap ? cap * 2 : 16; names = realloc(names, sizeof(char *) * cap); }
        names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, n, sizeof(char *), cmp_str);
    for (int i = 0; i < n - keep; i++) {
        snprintf(path, sizeof path, "%s/%s", dir, names[i]);
        unlink(path);
    }
    for (int i = 0; i < n; i++) free(names[i]);
    free(names);
    return 0;
}
