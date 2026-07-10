/* <sys/select.h> compat — winsock2 has select(), fd_set, FD_*. */
#ifndef YOS_WIN_COMPAT_SYS_SELECT_H
#define YOS_WIN_COMPAT_SYS_SELECT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <sys/time.h>

#endif
