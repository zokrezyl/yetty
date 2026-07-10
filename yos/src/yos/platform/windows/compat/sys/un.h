/* Minimal <sys/un.h> compat for MinGW. Modern Windows builds 17063+
 * actually support AF_UNIX via afunix.h, but the struct shape we
 * expose here is the POSIX one so generated code keeps compiling. */
#ifndef YOS_WIN_COMPAT_SYS_UN_H
#define YOS_WIN_COMPAT_SYS_UN_H

#include <sys/types.h>

#ifndef AF_UNIX
#define AF_UNIX 1
#endif

struct sockaddr_un {
    unsigned short sun_family;
    char           sun_path[108];
};

#endif
