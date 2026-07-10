/* <grp.h> compat — Windows has no /etc/group. yos's bridges return
 * a dummy "users" group. */
#ifndef YOS_WIN_COMPAT_GRP_H
#define YOS_WIN_COMPAT_GRP_H

#include <sys/types.h>

#ifndef _UID_T_DEFINED
typedef unsigned int uid_t;
typedef unsigned int gid_t;
#define _UID_T_DEFINED 1
#endif

struct group {
    char  *gr_name;
    char  *gr_passwd;
    gid_t  gr_gid;
    char **gr_mem;
};

#ifdef __cplusplus
extern "C" {
#endif

extern struct group *getgrnam(const char *name);
extern struct group *getgrgid(gid_t gid);
extern int           getgrnam_r(const char *n, struct group *g, char *b, size_t bsz, struct group **r);
extern int           getgrgid_r(gid_t gid,    struct group *g, char *b, size_t bsz, struct group **r);
extern struct group *getgrent(void);
extern void          setgrent(void);
extern void          endgrent(void);

#ifdef __cplusplus
}
#endif

#endif
