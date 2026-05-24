/* fs.c - POSIX filesystem helpers */

#include <yetty/yplatform/fs.h>
#include <dirent.h>
#include <libgen.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

int yetty_yplatform_mkdir(const char *path)
{
    return mkdir(path, 0755);
}

void yetty_yplatform_mkdir_p(const char *path)
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

int yetty_yplatform_file_exists(const char *path)
{
    return access(path, F_OK) == 0;
}

int yetty_yplatform_unlink(const char *path)
{
    return unlink(path);
}

int yetty_yplatform_chmod(const char *path, unsigned int mode)
{
    return chmod(path, (mode_t)mode);
}

int yetty_yplatform_file_is_regular(const char *path)
{
    if (!path) {
        return 0;
    }
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int yetty_yplatform_path_dirname(const char *path, char *out, size_t out_size)
{
    if (!path || !out || out_size == 0) {
        return -1;
    }
    /* POSIX dirname() may mutate its input — work on a private copy. */
    size_t len = strlen(path);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        return -1;
    }
    memcpy(copy, path, len + 1);
    char *d = dirname(copy);
    if (!d) {
        free(copy);
        return -1;
    }
    size_t dlen = strlen(d);
    if (dlen + 1 > out_size) {
        free(copy);
        return -1;
    }
    memcpy(out, d, dlen + 1);
    free(copy);
    return 0;
}

char *yetty_yplatform_path_realpath(const char *path)
{
    if (!path) {
        return NULL;
    }
    return realpath(path, NULL); /* glibc/musl/BSD: NULL second arg → malloc */
}

struct yetty_yplatform_dir {
    DIR *handle;
    char *root; /* kept for stat() fallback when d_type is DT_UNKNOWN */
};

struct yetty_yplatform_dir *yetty_yplatform_dir_open(const char *path)
{
    if (!path) {
        return NULL;
    }
    DIR *h = opendir(path);
    if (!h) {
        return NULL;
    }
    struct yetty_yplatform_dir *d = calloc(1, sizeof(*d));
    if (!d) {
        closedir(h);
        return NULL;
    }
    d->handle = h;
    size_t len = strlen(path);
    d->root = malloc(len + 1);
    if (!d->root) {
        closedir(h);
        free(d);
        return NULL;
    }
    memcpy(d->root, path, len + 1);
    return d;
}

int yetty_yplatform_dir_next(struct yetty_yplatform_dir *d, struct yetty_yplatform_dir_entry *out)
{
    if (!d || !out) {
        return 0;
    }
    struct dirent *e = readdir(d->handle);
    if (!e) {
        return 0;
    }
    out->name = e->d_name;
    int is_dir = 0;
#ifdef DT_DIR
    if (e->d_type != DT_UNKNOWN) {
        is_dir = (e->d_type == DT_DIR) ? 1 : 0;
    } else
#endif
    {
        char p[4096];
        int n = snprintf(p, sizeof(p), "%s/%s", d->root, e->d_name);
        if (n > 0 && (size_t)n < sizeof(p)) {
            struct stat st;
            if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
                is_dir = 1;
            }
        }
    }
    out->is_dir = is_dir;
    return 1;
}

void yetty_yplatform_dir_close(struct yetty_yplatform_dir *d)
{
    if (!d) {
        return;
    }
    if (d->handle) {
        closedir(d->handle);
    }
    free(d->root);
    free(d);
}
