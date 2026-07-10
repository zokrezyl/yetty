/* <net/if.h> compat — Windows iphlpapi exposes adapter info via its
 * own API, not POSIX struct ifreq. We provide just enough struct
 * shape so generated bridges parse; ioctl(SIOCGIF*) calls return
 * ENOSYS on Windows. */
#ifndef YOS_WIN_COMPAT_NET_IF_H
#define YOS_WIN_COMPAT_NET_IF_H

#include <stddef.h>
#include <stdint.h>
#include <sys/socket.h>

#ifndef IF_NAMESIZE
#define IF_NAMESIZE  16
#endif
#define IFNAMSIZ     IF_NAMESIZE

struct ifreq {
    char ifr_name[IF_NAMESIZE];
    union {
        struct sockaddr ifr_addr;
        struct sockaddr ifr_dstaddr;
        struct sockaddr ifr_broadaddr;
        struct sockaddr ifr_netmask;
        struct sockaddr ifr_hwaddr;
        short           ifr_flags;
        int             ifr_ifindex;
        int             ifr_metric;
        int             ifr_mtu;
        char            ifr_slave[IF_NAMESIZE];
        char            ifr_newname[IF_NAMESIZE];
        char           *ifr_data;
    } ifr_ifru;
};
#define ifr_addr      ifr_ifru.ifr_addr
#define ifr_dstaddr   ifr_ifru.ifr_dstaddr
#define ifr_broadaddr ifr_ifru.ifr_broadaddr
#define ifr_netmask   ifr_ifru.ifr_netmask
#define ifr_hwaddr    ifr_ifru.ifr_hwaddr
#define ifr_flags     ifr_ifru.ifr_flags
#define ifr_ifindex   ifr_ifru.ifr_ifindex
#define ifr_metric    ifr_ifru.ifr_metric
#define ifr_mtu       ifr_ifru.ifr_mtu
#define ifr_slave     ifr_ifru.ifr_slave
#define ifr_newname   ifr_ifru.ifr_newname
#define ifr_data      ifr_ifru.ifr_data

struct ifconf {
    int                 ifc_len;
    union {
        char           *ifcu_buf;
        struct ifreq   *ifcu_req;
    } ifc_ifcu;
};
#define ifc_buf       ifc_ifcu.ifcu_buf
#define ifc_req       ifc_ifcu.ifcu_req

/* ifa_* flags */
#define IFF_UP          0x1
#define IFF_BROADCAST   0x2
#define IFF_DEBUG       0x4
#define IFF_LOOPBACK    0x8
#define IFF_POINTOPOINT 0x10
#define IFF_NOTRAILERS  0x20
#define IFF_RUNNING     0x40
#define IFF_NOARP       0x80
#define IFF_PROMISC     0x100
#define IFF_ALLMULTI    0x200
#define IFF_MASTER      0x400
#define IFF_SLAVE       0x800
#define IFF_MULTICAST   0x1000
#define IFF_PORTSEL     0x2000
#define IFF_AUTOMEDIA   0x4000
#define IFF_DYNAMIC     0x8000

#ifdef __cplusplus
extern "C" {
#endif

extern unsigned int if_nametoindex(const char *name);
extern char *       if_indextoname(unsigned int idx, char *name);

#ifdef __cplusplus
}
#endif

#endif
