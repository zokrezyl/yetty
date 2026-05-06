#ifndef _LIBSLIRP_H
#define _LIBSLIRP_H

#ifdef CONFIG_SLIRP

#include <netinet/in.h>

struct Slirp;
typedef struct Slirp Slirp;

int get_dns_addr(struct in_addr *pdns_addr);

Slirp *slirp_init(int restricted, struct in_addr vnetwork,
                  struct in_addr vnetmask, struct in_addr vhost,
                  const char *vhostname, const char *tftp_path,
                  const char *bootfile, struct in_addr vdhcp_start,
                  struct in_addr vnameserver, void *opaque);
void slirp_cleanup(Slirp *slirp);

void slirp_select_fill(Slirp *slirp, int *pnfds,
                       fd_set *readfds, fd_set *writefds, fd_set *xfds);

void slirp_select_poll(Slirp *slirp,
                       fd_set *readfds, fd_set *writefds, fd_set *xfds,
                       int select_error);

void slirp_input(Slirp *slirp, const uint8_t *pkt, int pkt_len);

/* you must provide the following functions: */
int slirp_can_output(void *opaque);
void slirp_output(void *opaque, const uint8_t *pkt, int pkt_len);

int slirp_add_hostfwd(Slirp *slirp, int is_udp,
                      struct in_addr host_addr, int host_port,
                      struct in_addr guest_addr, int guest_port);
int slirp_remove_hostfwd(Slirp *slirp, int is_udp,
                         struct in_addr host_addr, int host_port);
int slirp_add_exec(Slirp *slirp, int do_pty, const void *args,
                   struct in_addr *guest_addr, int guest_port);

/* Pre-populate slirp's guest-MAC ARP cache. Without this, the very
 * first packet slirp tries to deliver to the guest is dropped by
 * if_encap (broadcast ARP request sent in its place); recovery relies
 * on the TCP retransmit timer AND the guest replying to the broadcast
 * ARP, which is brittle if the guest has done no outbound traffic yet
 * (e.g. static-IP /init that never DHCPs). The caller passes the same
 * MAC the virtio-net config advertises to the guest, so the cache
 * matches what eth0 will use. */
void slirp_set_client_mac(Slirp *slirp, const uint8_t mac[6]);

void slirp_socket_recv(Slirp *slirp, struct in_addr guest_addr,
                       int guest_port, const uint8_t *buf, int size);
size_t slirp_socket_can_recv(Slirp *slirp, struct in_addr guest_addr,
                             int guest_port);
int slirp_get_time_ms(void);

#else /* !CONFIG_SLIRP */

static inline void slirp_select_fill(int *pnfds, fd_set *readfds,
                                     fd_set *writefds, fd_set *xfds) { }

static inline void slirp_select_poll(fd_set *readfds, fd_set *writefds,
                                     fd_set *xfds, int select_error) { }
#endif /* !CONFIG_SLIRP */

#endif
