/*
 * yplatform/fs.h - Cross-platform filesystem helpers
 */

#ifndef YETTY_YPLATFORM_FS_H
#define YETTY_YPLATFORM_FS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Create a single directory (returns 0 on success or if already exists) */
int yetty_yplatform_mkdir(const char *path);

/* Create directory and all parent directories */
void yetty_yplatform_mkdir_p(const char *path);

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

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_FS_H */
