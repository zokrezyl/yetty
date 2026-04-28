/* fs.c - POSIX filesystem helpers */

#include <yetty/yplatform/fs.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

int yplatform_mkdir(const char *path)
{
    return mkdir(path, 0755);
}

void yplatform_mkdir_p(const char *path)
{
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);

    size_t len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = '\0';
    }

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

int yplatform_file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

int yplatform_unlink(const char *path)
{
    return unlink(path);
}

int yplatform_chmod(const char *path, unsigned int mode)
{
    return chmod(path, (mode_t)mode);
}
