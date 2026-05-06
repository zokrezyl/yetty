/*
 * slirp chr-backend — non-OS-socket "character device" attached to a
 * slirp inbound TCP connection.
 *
 * Lets a host-side caller (e.g. yetty's tinyemu-bridge.c on wasm)
 * inject a fully-synthetic inbound TCP connection into a guest port,
 * with no underlying OS socket. The slirp `struct socket` gets
 * `so->s = -1` and `so->extra` pointing at one of these backends.
 * The send hook fires whenever the guest writes bytes (sowrite via
 * the chr-backend dispatch); slirp_chr_input() in the other
 * direction stuffs host-supplied bytes into the socket's send buffer
 * so the next tcp_output ships them to the guest.
 *
 * Strictly additive — backend-less sockets (every desktop user)
 * never trigger the chr path.
 */

#ifndef SLIRP_CHR_BACKEND_H
#define SLIRP_CHR_BACKEND_H

#include "libslirp.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct slirp_chr_backend;
struct socket;

/* Owned by the caller. The send callback fires from slirp's TCP
 * send path whenever the guest writes bytes the slirp socket would
 * normally push to the OS via writev/send. The buffer is owned by
 * slirp; the callee must not retain it past the call.
 *
 * No corresponding "recv" callback — the host-side feed direction
 * goes through slirp_chr_input() on the host's schedule. */
struct slirp_chr_backend_ops {
    void (*send)(void *ctx, const void *buf, size_t len);
};

struct slirp_chr_backend {
    const struct slirp_chr_backend_ops *ops;
    void *ctx;
};

/*
 * Open a synthetic inbound TCP connection to (guest_addr, guest_port).
 * Returns the newly allocated `struct socket *` (also tracked in
 * slirp's tcb list) on success, NULL on failure.
 *
 * The `backend` pointer must outlive the socket; slirp stores it in
 * so->extra. Caller is responsible for tearing it down via
 * slirp_chr_close() before freeing the backend storage.
 *
 * The local side of the synthetic connection is assigned a fake
 * source IP (slirp's vhost_addr) and an ephemeral port chosen by
 * tcp_iss bumping; this guarantees uniqueness against existing
 * sockets in the same slirp instance, which is what
 * slirp_find_ctl_socket and the TCP fastq lookup key on.
 */
struct socket *slirp_chr_open(Slirp *slirp,
                              struct in_addr guest_addr, int guest_port,
                              struct slirp_chr_backend *backend);

/*
 * Push `len` bytes from the host into the synthetic connection's
 * send buffer, then kick tcp_output so the guest sees them on the
 * very next slirp tick. Returns the number of bytes accepted (may
 * be less than len if the socket buffer is full — caller should
 * retry later).
 */
size_t slirp_chr_input(struct socket *so, const void *buf, size_t len);

/*
 * Close + free the synthetic connection. Sends FIN to the guest
 * (via tcp_drop / tcp_sockclosed) and removes the socket from
 * slirp's tcb list. After this returns, `so` is invalid and the
 * caller should drop its backend pointer too.
 */
void slirp_chr_close(struct socket *so);

#ifdef __cplusplus
}
#endif

#endif /* SLIRP_CHR_BACKEND_H */
