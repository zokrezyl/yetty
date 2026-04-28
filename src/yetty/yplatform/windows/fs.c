/* fs.c - Windows filesystem helpers */

#include <yetty/yplatform/fs.h>
#include <direct.h>
#include <io.h>
#include <string.h>
#include <stdio.h>

int yplatform_mkdir(const char *path)
{
    return _mkdir(path);
}

void yplatform_mkdir_p(const char *path)
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

int yplatform_file_exists(const char *path)
{
    return _access(path, 0) == 0;
}

int yplatform_unlink(const char *path)
{
    return _unlink(path);
}

int yplatform_chmod(const char *path, unsigned int mode)
{
    (void)path;
    (void)mode;
    return 0;
}
