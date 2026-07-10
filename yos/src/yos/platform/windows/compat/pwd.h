/* <pwd.h> compat — Windows has no /etc/passwd. yos's bridges fabricate
 * a single entry from environment + GetUserName. */
#ifndef YOS_WIN_COMPAT_PWD_H
#define YOS_WIN_COMPAT_PWD_H

#include <sys/types.h>

/* POSIX types MSVC's <sys/types.h> doesn't ship. Match the definitions
 * in posix_extras.h. */
#ifndef _UID_T_DEFINED
typedef unsigned int uid_t;
typedef unsigned int gid_t;
#define _UID_T_DEFINED 1
#endif

struct passwd {
    char  *pw_name;
    char  *pw_passwd;
    uid_t  pw_uid;
    gid_t  pw_gid;
    char  *pw_gecos;
    char  *pw_dir;
    char  *pw_shell;
};

#ifdef __cplusplus
extern "C" {
#endif

extern struct passwd *getpwnam(const char *name);
extern struct passwd *getpwuid(uid_t uid);
extern int            getpwnam_r(const char *n, struct passwd *p, char *b, size_t bsz, struct passwd **r);
extern int            getpwuid_r(uid_t uid,    struct passwd *p, char *b, size_t bsz, struct passwd **r);
extern struct passwd *getpwent(void);
extern void           setpwent(void);
extern void           endpwent(void);

#ifdef __cplusplus
}
#endif

#endif
