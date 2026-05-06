/*
 * slirp chr-backend — synthetic inbound TCP connection without an OS
 * socket. See chr-backend.h for the rationale.
 *
 * Strictly additive: this file just adds three new public functions
 * (slirp_chr_open / slirp_chr_input / slirp_chr_close). The single
 * touch-up in existing slirp code is the so->s == -1 && so->extra
 * branch in slirp_send (slirp.c), which already had a `#if 0`
 * placeholder for exactly this pattern in the QEMU upstream.
 */

#include "slirp.h"
#include "chr-backend.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Synthesize an inbound TCP SYN to (guest_addr:guest_port). The new
 * socket is backed by `backend` instead of an OS fd; slirp_send
 * routes guest output through backend->ops->send. tcp_output()
 * fires the SYN immediately so the guest's listener (e.g. telnetd
 * on port 23) sees the connect attempt on the very next slirp tick. */
struct socket *slirp_chr_open(Slirp *slirp,
                              struct in_addr guest_addr, int guest_port,
                              struct slirp_chr_backend *backend)
{
    struct socket *so;
    struct tcpcb *tp;

    if (!slirp || !backend || !backend->ops || !backend->ops->send) {
        return NULL;
    }

    so = socreate(slirp);
    if (!so) {
        return NULL;
    }

    /* tcp_attach gives us so_snd / so_rcv buffers + a tcpcb, AND
     * inserts the socket into slirp->tcb so tcp_input can find it
     * by 4-tuple. tcp_listen() skipped this because listening
     * sockets don't carry buffers; we DO need them — bytes flow
     * both ways. */
    if (tcp_attach(so) < 0) {
        free(so);                     /* NOT sofree — no tcpcb to clean. */
        return NULL;
    }

    /* No OS fd. extra carries the chr-backend; slirp_send picks
     * this up to dispatch guest output. Marking the socket
     * SS_NOFDREF (and so->s == -1) keeps slirp_select_fill /
     * _poll from ever touching the (non-existent) OS socket. */
    so->s = -1;
    so->so_state |= SS_NOFDREF | SS_INCOMING;
    so->extra = backend;

    /* Address layout — from slirp's POV the guest is on the "local"
     * side of the connection, the host (us) on the "foreign" side.
     * That's what tcp_listen sets up too; we mirror it.
     *
     * laddr/lport = guest endpoint (the SYN destination).
     * faddr      = vhost_addr (slirp's well-known host IP, 10.0.2.2),
     *              the source the guest will reply to.
     * fport      = ephemeral; uniqueness against existing sockets
     *              is critical because slirp_find_ctl_socket and the
     *              fastq lookup key on (faddr, fport, laddr, lport).
     *              Use the slirp tcp_iss as a cheap unique stream
     *              and bump it; tcp_iss is already used as the ISS,
     *              so reusing the low 16 bits as an ephemeral port
     *              gives us monotonic uniqueness without a separate
     *              counter. */
    so->so_laddr = guest_addr;
    so->so_lport = htons((uint16_t)guest_port);
    so->so_faddr = slirp->vhost_addr;
    /* Ephemeral source port: pick from the dynamic range
     * (49152-65535) and bump tcp_iss to avoid stomping on a port
     * that's already in use against the same destination. */
    so->so_fport = htons((uint16_t)(49152 + (slirp->tcp_iss & 0x3fff)));
    slirp->tcp_iss += 0x4000;

    /* Drive the TCP state machine: SYN_SENT → tcp_output → SYN
     * goes to guest. Mirrors the tail of tcp_connect() in
     * tcp_subr.c, with one critical difference:
     *
     *   tp->t_timer[TCPT_KEEP] is NOT armed.
     *
     * The KEEP timer fires after ~75 s and (per tcp_timer.c::TCPT_KEEP)
     * UNCONDITIONALLY drops the connection if state < ESTABLISHED.
     * Yetty's webasm pty opens this synthetic connection as soon as
     * the VM signals vm-ready — but the in-VM telnetd doesn't bind
     * port 23 until alpine OpenRC + busybox-extras runs (often >75 s
     * on the slow wasm interpreter). Without the keep timer the
     * normal TCP retransmission timer (TCPT_REXMT, exponential
     * backoff, ~7 minutes total) drives the SYN until telnetd is
     * actually listening — at which point the SYN-ACK comes in,
     * solookup matches our 4-tuple, and the connection establishes
     * normally.
     *
     * Side effect: keep-alive on the established connection is also
     * disabled, but yetty's terminal is interactive — the user typing
     * keeps the connection live; idle keep-alive isn't worth the
     * SYN-SENT corner case. */
    tp = sototcpcb(so);
    (void)tcp_mss(tp, 0);
    tcp_template(tp);

    tp->t_state = TCPS_SYN_SENT;
    tp->t_timer[TCPT_KEEP] = 0;  /* see comment above */
    tp->iss = slirp->tcp_iss;
    slirp->tcp_iss += TCP_ISSINCR / 2;
    tcp_sendseqinit(tp);

    if (tcp_output(tp) < 0) {
        /* tcp_output failed — drop the socket cleanly. tcp_drop frees
         * the tcpcb and unlinks the socket. */
        tcp_drop(tp, 0);
        return NULL;
    }

    return so;
}

/* Push host-supplied bytes into the synthetic socket's send buffer
 * and immediately call tcp_output so segments leave on this slirp
 * tick. Returns the number of bytes accepted. */
size_t slirp_chr_input(struct socket *so, const void *buf, size_t len)
{
    int n;

    if (!so || !buf || len == 0) {
        return 0;
    }
    if (so->s != -1 || so->extra == NULL) {
        /* Not a chr socket. Refuse to scribble on so_snd here. */
        return 0;
    }
    if (so->so_state & (SS_FCANTSENDMORE | SS_FWDRAIN)) {
        return 0;
    }

    /* soreadbuf is the existing slirp helper that copies a buffer
     * into so_snd respecting wrap-around. Returns size on success,
     * -1 if the buffer wouldn't fit (which marks the socket
     * SS_FCANTRCVMORE + closes the tcpcb on its own). */
    n = soreadbuf(so, (const char *)buf, (int)len);
    if (n <= 0) {
        return 0;
    }
    /* Kick the TX side. Without this the bytes sit in so_snd
     * until the next ack from the guest triggers an output. */
    tcp_output(sototcpcb(so));
    return (size_t)n;
}

/* Initiate a clean close. Mirrors what soisfdisconnected /
 * tcp_drop do for fd-backed sockets when the host closes. */
void slirp_chr_close(struct socket *so)
{
    if (!so) {
        return;
    }
    /* Half-close: no more host bytes coming. tcp_sockclosed will
     * advance the TCP state machine to FIN_WAIT_1 and emit FIN. */
    sofcantsendmore(so);
    if (so->so_tcpcb) {
        tcp_sockclosed(so->so_tcpcb);
    }
    /* Drop the chr backend pointer so any in-flight slirp_send
     * skips the chr path and falls through to the (no-op) send()
     * on so->s == -1. Caller is free to free its backend now. */
    so->extra = NULL;
}
