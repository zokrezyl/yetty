/* <netinet/in.h> compat — forward to winsock2/ws2tcpip. */
#ifndef YOS_WIN_COMPAT_NETINET_IN_H
#define YOS_WIN_COMPAT_NETINET_IN_H

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>

#endif
