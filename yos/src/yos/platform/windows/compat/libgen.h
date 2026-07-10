/* <libgen.h> compat — basename / dirname. POSIX-modifying variants. */
#ifndef YOS_WIN_COMPAT_LIBGEN_H
#define YOS_WIN_COMPAT_LIBGEN_H

#ifdef __cplusplus
extern "C" {
#endif

extern char *basename(char *path);
extern char *dirname (char *path);

#ifdef __cplusplus
}
#endif

#endif
