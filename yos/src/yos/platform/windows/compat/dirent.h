/* <dirent.h> — MSVC/Windows-SDK has no dirent. Provide a complete
 * implementation backed by FindFirstFileW / FindNextFileW.
 *
 * Surface implemented:
 *   DIR (opaque), struct dirent (POSIX shape + d_off / d_type),
 *   opendir / fdopendir / readdir / readdir_r / rewinddir / closedir /
 *   dirfd / telldir / seekdir / scandir.
 *
 * Implementations live in compat/dirent_win32.c. */
#ifndef YOS_WIN_COMPAT_DIRENT_H
#define YOS_WIN_COMPAT_DIRENT_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#ifndef DT_UNKNOWN
#define DT_UNKNOWN 0
#define DT_FIFO    1
#define DT_CHR     2
#define DT_DIR     4
#define DT_BLK     6
#define DT_REG     8
#define DT_LNK    10
#define DT_SOCK   12
#endif

#ifndef NAME_MAX
#define NAME_MAX  255
#endif

struct dirent {
    long long d_ino;
    long long d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[NAME_MAX + 1];
};

typedef struct yos_win_dir DIR;

#ifdef __cplusplus
extern "C" {
#endif

extern DIR *           opendir(const char *path);
extern DIR *           fdopendir(int fd);
extern struct dirent * readdir(DIR *dir);
extern int             readdir_r(DIR *dir, struct dirent *entry, struct dirent **result);
extern int             closedir(DIR *dir);
extern void            rewinddir(DIR *dir);
extern int             dirfd(DIR *dir);
extern long            telldir(DIR *dir);
extern void            seekdir(DIR *dir, long pos);
extern int             scandir(const char *dir, struct dirent ***namelist,
                               int (*sel)(const struct dirent *),
                               int (*cmp)(const struct dirent **, const struct dirent **));
extern int             alphasort(const struct dirent **a, const struct dirent **b);

#ifdef __cplusplus
}
#endif

#endif /* YOS_WIN_COMPAT_DIRENT_H */
