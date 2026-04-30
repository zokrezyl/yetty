/*
 * yplatform/fs.h - Cross-platform filesystem helpers
 */

#ifndef YETTY_YPLATFORM_FS_H
#define YETTY_YPLATFORM_FS_H

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

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_FS_H */
