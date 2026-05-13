/* fs.c - Windows filesystem helpers */

#include <yetty/yplatform/fs.h>
#include <direct.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

int yetty_yplatform_mkdir(const char *path)
{
    return _mkdir(path);
}

void yetty_yplatform_mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);

    size_t len = strlen(tmp);
    if (len > 0 && (tmp[len - 1] == '/' || tmp[len - 1] == '\\')) {
        tmp[len - 1] = '\0';
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            _mkdir(tmp);
            *p = '/';
        }
    }
    _mkdir(tmp);
}

int yetty_yplatform_file_exists(const char *path)
{
    return _access(path, 0) == 0;
}

int yetty_yplatform_unlink(const char *path)
{
    return _unlink(path);
}

int yetty_yplatform_chmod(const char *path, unsigned int mode)
{
    (void)path;
    (void)mode;
    return 0;
}

int yetty_yplatform_file_is_regular(const char *path)
{
    if (!path) return 0;
    struct _stat st;
    /* MSVC has _S_IFREG; the POSIX S_ISREG macro isn't defined. */
    return _stat(path, &st) == 0 && (st.st_mode & _S_IFREG) != 0;
}

int yetty_yplatform_path_dirname(const char *path, char *out, size_t out_size)
{
    if (!path || !out || out_size == 0) return -1;
    size_t len = strlen(path);
    /* Find the last path separator — accept both forward and back slashes
     * (Windows paths happily mix them). */
    size_t sep = (size_t)-1;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/' || path[i] == '\\') sep = i;
    }
    if (sep == (size_t)-1) {
        /* No separator → POSIX dirname returns "." */
        if (out_size < 2) return -1;
        out[0] = '.'; out[1] = '\0';
        return 0;
    }
    /* "/foo" → "/"; "C:\\foo" → "C:\\"; strip duplicate trailing separators */
    while (sep > 0 && (path[sep - 1] == '/' || path[sep - 1] == '\\')) sep--;
    size_t need = (sep == 0) ? 1 : sep;  /* at least the leading separator */
    if (need + 1 > out_size) return -1;
    if (sep == 0) {
        out[0] = path[0];  /* the bare separator */
        out[1] = '\0';
    } else {
        memcpy(out, path, sep);
        out[sep] = '\0';
    }
    return 0;
}

char *yetty_yplatform_path_realpath(const char *path)
{
    if (!path) return NULL;
    /* _fullpath() with NULL buffer + 0 length malloc()s the result. */
    return _fullpath(NULL, path, 0);
}
