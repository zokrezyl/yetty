/* Minimal <sys/utsname.h> compat. */
#ifndef YOS_WIN_COMPAT_SYS_UTSNAME_H
#define YOS_WIN_COMPAT_SYS_UTSNAME_H

struct utsname {
    char sysname [65];
    char nodename[65];
    char release [65];
    char version [65];
    char machine [65];
};

#endif
