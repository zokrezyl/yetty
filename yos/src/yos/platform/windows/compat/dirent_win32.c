/* dirent_win32.c — Win32 FindFirstFile-backed POSIX dirent implementation.
 *
 * Symbol-mode: every entry that exists on POSIX returns sensible values.
 * d_type is filled from the Win32 file attributes (DIR/REG/LNK).
 * d_ino is the Win32 FileIndex when available, otherwise a hash of the
 * d_name. */

#include "dirent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

struct yos_win_dir {
    HANDLE          handle;
    WIN32_FIND_DATAW first;
    int             have_first;       /* first slot still pending? */
    struct dirent   entry;            /* readdir return slot */
    long            pos;              /* telldir() value */
    int             fd;               /* dirfd() — synthetic, -1 if unset */
    wchar_t        *pattern;          /* "<path>\*" for rewinddir */
};

/* utf-8 → utf-16 with malloc; caller frees. NULL on alloc failure. */
static wchar_t *utf8_to_wide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) { errno = EINVAL; return NULL; }
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w) { errno = ENOMEM; return NULL; }
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

/* utf-16 → utf-8 into dst (cap bytes); returns 0 on success. */
static int wide_to_utf8(const wchar_t *w, char *dst, size_t cap) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, dst, (int)cap, NULL, NULL);
    return (n > 0) ? 0 : -1;
}

static void fill_entry(struct dirent *e, const WIN32_FIND_DATAW *fd) {
    if (wide_to_utf8(fd->cFileName, e->d_name, sizeof e->d_name) < 0)
        e->d_name[0] = 0;
    e->d_off    = 0;
    e->d_reclen = (unsigned short)sizeof(*e);
    if (fd->dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        e->d_type = DT_LNK;
    else if (fd->dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        e->d_type = DT_DIR;
    else
        e->d_type = DT_REG;
    /* Cheap fnv-1a-ish hash so d_ino is at least stable across reads. */
    uint64_t h = 0xcbf29ce484222325ULL;
    for (size_t i = 0; e->d_name[i]; i++) {
        h ^= (unsigned char)e->d_name[i];
        h *= 0x100000001b3ULL;
    }
    e->d_ino = (long long)h;
}

DIR *opendir(const char *path) {
    if (!path) { errno = EINVAL; return NULL; }
    /* "<path>\*" pattern. */
    size_t len = strlen(path);
    char *pat = (char *)malloc(len + 3);
    if (!pat) { errno = ENOMEM; return NULL; }
    memcpy(pat, path, len);
    pat[len] = '\\';
    pat[len + 1] = '*';
    pat[len + 2] = 0;

    wchar_t *wpat = utf8_to_wide(pat);
    free(pat);
    if (!wpat) return NULL;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(wpat, FindExInfoBasic, &fd,
                                FindExSearchNameMatch, NULL,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD e = GetLastError();
        free(wpat);
        if (e == ERROR_FILE_NOT_FOUND) {
            /* Empty directory still gives us a DIR with no entries. */
        } else {
            errno = (e == ERROR_PATH_NOT_FOUND) ? ENOENT : EACCES;
            return NULL;
        }
    }

    DIR *d = (DIR *)calloc(1, sizeof *d);
    if (!d) {
        if (h != INVALID_HANDLE_VALUE) FindClose(h);
        free(wpat);
        errno = ENOMEM;
        return NULL;
    }
    d->handle     = h;
    d->first      = fd;
    d->have_first = (h != INVALID_HANDLE_VALUE) ? 1 : 0;
    d->pos        = 0;
    d->fd         = -1;
    d->pattern    = wpat;
    return d;
}

DIR *fdopendir(int fd) {
    /* No portable way to recover the directory path from a Win32
     * file-descriptor without GetFinalPathNameByHandle. yos doesn't
     * actually call fdopendir on host paths — the wasm guest's
     * fdopendir is routed through the virtual fd table. */
    (void)fd;
    errno = ENOSYS;
    return NULL;
}

struct dirent *readdir(DIR *d) {
    if (!d) { errno = EBADF; return NULL; }
    if (d->handle == INVALID_HANDLE_VALUE) return NULL;
    if (d->have_first) {
        d->have_first = 0;
        fill_entry(&d->entry, &d->first);
        d->pos++;
        return &d->entry;
    }
    WIN32_FIND_DATAW fd;
    if (!FindNextFileW(d->handle, &fd)) return NULL;
    fill_entry(&d->entry, &fd);
    d->pos++;
    return &d->entry;
}

int readdir_r(DIR *d, struct dirent *entry, struct dirent **result) {
    struct dirent *r = readdir(d);
    if (!r) {
        if (result) *result = NULL;
        return 0;
    }
    *entry = *r;
    if (result) *result = entry;
    return 0;
}

int closedir(DIR *d) {
    if (!d) return EBADF;
    if (d->handle != INVALID_HANDLE_VALUE) FindClose(d->handle);
    free(d->pattern);
    free(d);
    return 0;
}

void rewinddir(DIR *d) {
    if (!d || !d->pattern) return;
    if (d->handle != INVALID_HANDLE_VALUE) FindClose(d->handle);
    d->handle = FindFirstFileExW(d->pattern, FindExInfoBasic, &d->first,
                                 FindExSearchNameMatch, NULL,
                                 FIND_FIRST_EX_LARGE_FETCH);
    d->have_first = (d->handle != INVALID_HANDLE_VALUE) ? 1 : 0;
    d->pos = 0;
}

int  dirfd(DIR *d)      { return d ? d->fd : -1; }
long telldir(DIR *d)    { return d ? d->pos : -1; }
void seekdir(DIR *d, long pos) {
    /* Brute-force: rewind, then readdir() until pos. */
    if (!d) return;
    rewinddir(d);
    while (d->pos < pos && readdir(d)) { }
}

int alphasort(const struct dirent **a, const struct dirent **b) {
    return strcmp((*a)->d_name, (*b)->d_name);
}

int scandir(const char *path, struct dirent ***namelist,
            int (*sel)(const struct dirent *),
            int (*cmp)(const struct dirent **, const struct dirent **)) {
    DIR *d = opendir(path);
    if (!d) return -1;
    size_t cap = 16, n = 0;
    struct dirent **arr = (struct dirent **)malloc(cap * sizeof *arr);
    if (!arr) { closedir(d); errno = ENOMEM; return -1; }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (sel && !sel(e)) continue;
        struct dirent *copy = (struct dirent *)malloc(sizeof *copy);
        if (!copy) goto fail_oom;
        *copy = *e;
        if (n == cap) {
            cap *= 2;
            struct dirent **bigger = (struct dirent **)realloc(arr, cap * sizeof *arr);
            if (!bigger) { free(copy); goto fail_oom; }
            arr = bigger;
        }
        arr[n++] = copy;
    }
    closedir(d);
    if (cmp) {
        qsort(arr, n, sizeof *arr,
              (int (*)(const void *, const void *))cmp);
    }
    *namelist = arr;
    return (int)n;

fail_oom:
    for (size_t i = 0; i < n; i++) free(arr[i]);
    free(arr);
    closedir(d);
    errno = ENOMEM;
    return -1;
}
