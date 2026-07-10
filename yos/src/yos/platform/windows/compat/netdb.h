/* <netdb.h> compat — Windows network DB lives in winsock2.h / ws2tcpip.h.
 *
 * Pull both, then add POSIX-only error codes that those headers don't
 * declare (EAI_AGAIN etc. — Windows uses WSA-prefixed values). */
#ifndef YOS_WIN_COMPAT_NETDB_H
#define YOS_WIN_COMPAT_NETDB_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#ifndef EAI_AGAIN
#define EAI_AGAIN     WSATRY_AGAIN
#endif
#ifndef EAI_BADFLAGS
#define EAI_BADFLAGS  WSAEINVAL
#endif
#ifndef EAI_FAIL
#define EAI_FAIL      WSANO_RECOVERY
#endif
#ifndef EAI_FAMILY
#define EAI_FAMILY    WSAEAFNOSUPPORT
#endif
#ifndef EAI_MEMORY
#define EAI_MEMORY    WSA_NOT_ENOUGH_MEMORY
#endif
#ifndef EAI_NODATA
#define EAI_NODATA    WSANO_DATA
#endif
#ifndef EAI_NONAME
#define EAI_NONAME    WSAHOST_NOT_FOUND
#endif
#ifndef EAI_SERVICE
#define EAI_SERVICE   WSATYPE_NOT_FOUND
#endif
#ifndef EAI_SOCKTYPE
#define EAI_SOCKTYPE  WSAESOCKTNOSUPPORT
#endif
#ifndef EAI_SYSTEM
#define EAI_SYSTEM    -11
#endif
#ifndef EAI_OVERFLOW
#define EAI_OVERFLOW  -12
#endif
#ifndef EAI_ADDRFAMILY
#define EAI_ADDRFAMILY -13
#endif

#ifndef NI_MAXHOST
#define NI_MAXHOST  1025
#define NI_MAXSERV  32
#endif

#endif
