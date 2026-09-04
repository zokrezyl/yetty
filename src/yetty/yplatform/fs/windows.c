/* fs.c - Windows filesystem helpers */

#include <yetty/yplatform/fs.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <errno.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

int yetty_yplatform_mkdir(const char *path)
{
    return _mkdir(path);
}

struct yetty_ycore_void_result yetty_yplatform_mkdir_p(const char *path)
{
    if (!path || !*path) {
        return YETTY_ERR(yetty_ycore_void, "mkdir_p: empty path");
    }
    char tmp[1024];
    int written = snprintf(tmp, sizeof(tmp), "%s", path);
    if (written < 0 || (size_t)written >= sizeof(tmp)) {
        return YETTY_ERR(yetty_ycore_void, "mkdir_p: path too long");
    }

    size_t len = strlen(tmp);
    if (len > 0 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\')) {
        tmp[len - 1] = '\0';
    }

    for (char *cursor = tmp + 1; *cursor; cursor++) {
        if (*cursor == '/' || *cursor == '\\') {
            char saved = *cursor;
            *cursor = '\0';
            /* Parent steps: drive roots ("C:") and already-existing
             * components fail harmlessly; a real failure makes the final
             * _mkdir below fail too, which is where the error surfaces. */
            _mkdir(tmp);
            *cursor = saved;
        }
    }
    if (_mkdir(tmp) != 0 && errno != EEXIST) {
        return YETTY_ERR(yetty_ycore_void, "mkdir_p: cannot create directory");
    }
    /* EEXIST also fires when a regular file sits at `path` — verify. */
    struct _stat st;
    if (_stat(tmp, &st) != 0 || (st.st_mode & _S_IFMT) != _S_IFDIR) {
        return YETTY_ERR(yetty_ycore_void, "mkdir_p: path exists but is not a directory");
    }
    return YETTY_OK_VOID();
}

int yetty_yplatform_file_exists(const char *path)
{
    return _access(path, 0) == 0;
}

int yetty_yplatform_unlink(const char *path)
{
    return _unlink(path);
}

int yetty_yplatform_rmdir(const char *path)
{
    return _rmdir(path);
}

int yetty_yplatform_chmod(const char *path, unsigned int mode)
{
    (void)path;
    (void)mode;
    return 0;
}

int yetty_yplatform_file_is_regular(const char *path)
{
    if (!path) {
        return 0;
    }
    struct _stat st;
    /* MSVC has _S_IFREG; the POSIX S_ISREG macro isn't defined. */
    return _stat(path, &st) == 0 && (st.st_mode & _S_IFREG) != 0;
}

int yetty_yplatform_path_dirname(const char *path, char *out, size_t out_size)
{
    if (!path || !out || out_size == 0) {
        return -1;
    }
    size_t len = strlen(path);
    /* Find the last path separator — accept both forward and back slashes
     * (Windows paths happily mix them). */
    size_t sep = (size_t)-1;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/' || path[i] == '\\') {
            sep = i;
        }
    }
    if (sep == (size_t)-1) {
        /* No separator → POSIX dirname returns "." */
        if (out_size < 2) {
            return -1;
        }
        out[0] = '.';
        out[1] = '\0';
        return 0;
    }
    /* "/foo" → "/"; "C:\\foo" → "C:\\"; strip duplicate trailing separators */
    while (sep > 0 && (path[sep - 1] == '/' || path[sep - 1] == '\\')) {
        sep--;
    }
    size_t need = (sep == 0) ? 1 : sep; /* at least the leading separator */
    if (need + 1 > out_size) {
        return -1;
    }
    if (sep == 0) {
        out[0] = path[0]; /* the bare separator */
        out[1] = '\0';
    } else {
        memcpy(out, path, sep);
        out[sep] = '\0';
    }
    return 0;
}

char *yetty_yplatform_path_realpath(const char *path)
{
    if (!path) {
        return NULL;
    }
    /* _fullpath() with NULL buffer + 0 length malloc()s the result. */
    return _fullpath(NULL, path, 0);
}

struct yetty_yplatform_dir {
    HANDLE find;
    WIN32_FIND_DATAA fd;
    int started; /* 0 = FindFirstFile result not yet returned */
};

struct yetty_yplatform_dir *yetty_yplatform_dir_open(const char *path)
{
    if (!path) {
        return NULL;
    }
    size_t len = strlen(path);
    char *pattern = (char *)malloc(len + 3); /* path + optional sep + '*' + NUL */
    if (!pattern) {
        return NULL;
    }
    memcpy(pattern, path, len);
    /* Accept both separator styles in input; always emit one before '*'. */
    if (len == 0 || (path[len - 1] != '/' && path[len - 1] != '\\')) {
        pattern[len++] = '\\';
    }
    pattern[len++] = '*';
    pattern[len] = '\0';
    struct yetty_yplatform_dir *d = (struct yetty_yplatform_dir *)calloc(1, sizeof(*d));
    if (!d) {
        free(pattern);
        return NULL;
    }
    d->find = FindFirstFileA(pattern, &d->fd);
    free(pattern);
    if (d->find == INVALID_HANDLE_VALUE) {
        free(d);
        return NULL;
    }
    return d;
}

int yetty_yplatform_dir_next(struct yetty_yplatform_dir *d, struct yetty_yplatform_dir_entry *out)
{
    if (!d || !out || d->find == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (d->started) {
        if (!FindNextFileA(d->find, &d->fd)) {
            return 0;
        }
    } else {
        d->started = 1;
    }
    out->name = d->fd.cFileName;
    out->is_dir = (d->fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
    return 1;
}

void yetty_yplatform_dir_close(struct yetty_yplatform_dir *d)
{
    if (!d) {
        return;
    }
    if (d->find != INVALID_HANDLE_VALUE) {
        FindClose(d->find);
    }
    free(d);
}
