/*
 * yplatform/fs.h - Cross-platform filesystem helpers
 */

#ifndef YETTY_YPLATFORM_FS_H
#define YETTY_YPLATFORM_FS_H

#include <stddef.h>

#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create a single directory. Returns 0 only when this call created it;
 * -1 on error with errno set (EEXIST when it already exists), so callers
 * that need to claim a directory exclusively can rely on the result. */
int yetty_yplatform_mkdir(const char *path);

/* Remove an empty directory. Returns 0 on success, -1 on error (errno set). */
int yetty_yplatform_rmdir(const char *path);

/* Create directory and all parent directories. An already-existing
 * directory is success; anything else (permission denied, read-only
 * filesystem, a path component that is a regular file, an over-long
 * path) is an error naming the failing step. */
struct yetty_ycore_void_result yetty_yplatform_mkdir_p(const char *path);

/* Non-zero if a file or directory exists at path. */
int yetty_yplatform_file_exists(const char *path);

/* Remove a file. Returns 0 on success, -1 on error (errno set). */
int yetty_yplatform_unlink(const char *path);

/* Change file mode. Returns 0 on success, -1 on error (errno set).
 * On Windows this is a no-op since POSIX permission bits do not apply. */
int yetty_yplatform_chmod(const char *path, unsigned int mode);

/* Non-zero if `path` refers to an existing *regular* file (not a
 * directory, symlink-to-directory, device, FIFO, …). Use this when you
 * want stricter semantics than `yetty_yplatform_file_exists`. */
int yetty_yplatform_file_is_regular(const char *path);

/* Write the directory portion of `path` into `out` (no trailing slash).
 * Returns 0 on success, -1 if `out` is too small or `path`/`out` is
 * NULL. Mirrors POSIX dirname() but doesn't mutate the input. */
int yetty_yplatform_path_dirname(const char *path, char *out, size_t out_size);

/* Resolve `path` to an absolute, canonical path. Returns a malloc()'d
 * buffer the caller must free(), or NULL on error. On POSIX this maps
 * to realpath(path, NULL); on Win32 to _fullpath(NULL, path, 0). */
char *yetty_yplatform_path_realpath(const char *path);

/* Directory iteration. Single-threaded use only — the handle is not
 * thread-safe. Backed by opendir/readdir on POSIX and FindFirstFile /
 * FindNextFile on Win32. */
struct yetty_yplatform_dir;

struct yetty_yplatform_dir_entry {
    /* Basename only — no leading path. Storage owned by the handle and
     * valid only until the next yetty_yplatform_dir_next() / _close() on
     * the same handle. */
    const char *name;
    /* Non-zero if the entry is a directory. */
    int is_dir;
};

/* Open `path` for entry iteration. Returns NULL on error. */
struct yetty_yplatform_dir *yetty_yplatform_dir_open(const char *path);

/* Read the next entry. Returns 1 and fills *out on success, 0 at
 * end-of-directory or on error. Includes "." and ".." entries — filter
 * at the caller. */
int yetty_yplatform_dir_next(struct yetty_yplatform_dir *d, struct yetty_yplatform_dir_entry *out);

/* Close the handle. Safe on NULL. */
void yetty_yplatform_dir_close(struct yetty_yplatform_dir *d);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_FS_H */
