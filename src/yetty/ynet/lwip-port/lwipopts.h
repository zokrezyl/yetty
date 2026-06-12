/* lwIP options for yetty's in-browser userspace TCP/IP stack.
 *
 * NO_SYS=1 raw/callback API — there are no threads in the
 * single-threaded wasm build, so lwIP runs entirely from the browser
 * event loop: inbound ethernet frames from the relay WebSocket feed
 * netif->input(), a periodic tick calls sys_check_timeouts(), and the
 * tcp_* raw callbacks drive each connection. See src/yetty/ynet/.
 *
 * IPv4 only (the relays we target hand out v4 via DHCP); DHCP + DNS on
 * so the stack auto-configures and can dial hosts by name.
 */

#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/* ---- Symbol-collision rename (lwIP vs TinyEMU's slirp) -----------
 * TinyEMU bundles slirp, a BSD-derived TCP/IP stack that exports the
 * same classic names as lwIP (tcp_close, tcp_connect, tcp_input,
 * tcp_output, tcp_init, udp_init, udp_input, icmp_input). Both static
 * archives link into one wasm module, so wasm-ld sees duplicate
 * symbols. We rename lwIP's eight colliding symbols to lwip_*: this
 * header is included by every lwIP source (via lwip/opt.h) AND by our
 * ynet sources (via the lwIP public headers), so the definitions,
 * lwIP's internal references, and our call sites all rename together.
 * slirp keeps the original names — it never includes lwipopts.h. */
#define icmp_input lwip_icmp_input
#define tcp_close lwip_tcp_close
#define tcp_connect lwip_tcp_connect
#define tcp_init lwip_tcp_init
#define tcp_input lwip_tcp_input
#define tcp_output lwip_tcp_output
#define udp_init lwip_udp_init
#define udp_input lwip_udp_input

/* ---- No OS: raw API only ----------------------------------------- */
#define NO_SYS 1
#define SYS_LIGHTWEIGHT_PROT 0
#define LWIP_NETCONN 0
#define LWIP_SOCKET 0
#define LWIP_NETIF_API 0

/* ---- Protocol surface -------------------------------------------- */
#define LWIP_IPV4 1
#define LWIP_IPV6 0
#define LWIP_ARP 1
#define LWIP_ETHERNET 1
#define LWIP_DHCP 1
#define LWIP_DNS 1
#define LWIP_TCP 1
#define LWIP_UDP 1
#define LWIP_ICMP 1

/* ---- Callbacks we rely on ---------------------------------------- */
#define LWIP_NETIF_STATUS_CALLBACK 1
#define LWIP_NETIF_LINK_CALLBACK 1
#define LWIP_NETIF_HOSTNAME 1

/* ---- Memory / sizing --------------------------------------------- */
#define MEM_ALIGNMENT 4
#define MEM_SIZE (512 * 1024)
#define MEMP_NUM_TCP_PCB 16
#define MEMP_NUM_TCP_PCB_LISTEN 4
#define MEMP_NUM_TCP_SEG 64
#define PBUF_POOL_SIZE 64
#define TCP_MSS 1460
#define TCP_WND (16 * TCP_MSS)
#define TCP_SND_BUF (16 * TCP_MSS)
/* Keep the send queue >= 4*SND_BUF/MSS or lwIP's sanity check errors. */
#define TCP_SND_QUEUELEN ((4 * TCP_SND_BUF + TCP_MSS - 1) / TCP_MSS)

/* ---- Checksums: done in software (no NIC offload in the browser) -- */
#define CHECKSUM_GEN_IP 1
#define CHECKSUM_GEN_TCP 1
#define CHECKSUM_GEN_UDP 1
#define CHECKSUM_CHECK_IP 1
#define CHECKSUM_CHECK_TCP 1
#define CHECKSUM_CHECK_UDP 1

#define LWIP_STATS 0
#define LWIP_DEBUG 0

#endif /* LWIP_LWIPOPTS_H */
