/* DCS-over-PTY yclass transport — client side.
 *
 * send(bytes) appends to `outbuf`. recv() flushes `outbuf` (one DCS
 * envelope onto `write_fd`) before blocking on `read_fd`, then drives
 * an internal wire_statemachine to assemble inbound DCS envelopes
 * into `inbuf`, and hands the caller bytes from `inbuf` as they're
 * requested. */

#include <yetty/yclass/transport-dcs.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ywire/wire-statemachine.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h> /* read/write/close — MSVC ships no <unistd.h> */
#else
#include <poll.h>
#include <unistd.h>
#endif

/* How long recv() waits for the peer's reply before declaring the link dead.
 * recv() is only reached for request/response calls (the startup handshake:
 * translate_class + GET_ROOT); steady-state figure calls are one-way and never
 * recv(). A real host answers in well under a second; the generous bound just
 * keeps a host-less invocation from hanging forever instead of failing. */
#define DCS_RECV_TIMEOUT_MS 5000

/* How long the flush waits for the write fd to drain when a large envelope
 * overruns the PTY buffer (see the EAGAIN branch in dcs_flush_outbuf). */
#define DCS_SEND_TIMEOUT_MS 5000

struct dcs_transport {
    struct yetty_yclass_transport base;
    int read_fd;
    int write_fd;
    int dcs_code;
    int compressed;

    /* Outbound: yrpc send() calls accumulate here; flushed as one
     * envelope on the next recv(). */
    struct yetty_ycore_buffer outbuf;
    /* Reusable envelope scratch for the flush — cleared per flush, so a
     * long-lived session doesn't allocate a frame buffer per envelope. */
    struct yetty_ycore_buffer emit_buf;

    /* Inbound: wire_statemachine driven from `read_fd`; the buffered
     * handler appends decoded envelope bodies to `inbuf` and recv()
     * hands bytes out from `inbuf_off`. The SM doubles as the outbound
     * framer (encoder state is disjoint from the scanner state), keeping
     * one LZ4F context + scratch alive across the session. */
    struct yetty_ywire_wire_statemachine *sm;
    struct yetty_ycore_buffer inbuf;
    size_t inbuf_off;
    int eof;
};

/* Buffered handler — fires once per inbound envelope with the full
 * decoded body. Append it to inbuf for the next recv() call. */
static struct yetty_ycore_void_result dcs_on_envelope(void *userdata,
                                                      enum yetty_ywire_envelope_kind kind, int code,
                                                      const uint8_t *args, size_t args_len,
                                                      const uint8_t *payload, size_t payload_len)
{
    (void)kind;
    (void)code;
    (void)args;
    (void)args_len;
    struct dcs_transport *t = userdata;
    /* Reclaim space if everything that landed earlier has been
     * consumed. Avoids unbounded growth in long-lived sessions. */
    if (t->inbuf_off == t->inbuf.size) {
        yetty_ycore_buffer_clear(&t->inbuf);
        t->inbuf_off = 0;
    }
    return yetty_ycore_buffer_write(&t->inbuf, payload, payload_len);
}

static struct yetty_ycore_void_result dcs_flush_outbuf(struct dcs_transport *t)
{
    if (t->outbuf.size == 0) {
        return YETTY_OK_VOID();
    }
    /* Frame through the transport's long-lived SM into the reusable
     * emit_buf — steady-state figure calls flush at UI rate, so this path
     * must not spin up a transient SM (and LZ4F context) per envelope. */
    yetty_ycore_buffer_clear(&t->emit_buf);
    struct yetty_ycore_void_result frame_res = yetty_ywire_wire_statemachine_start_write(
        t->sm, YETTY_YWIRE_ENVELOPE_DCS, t->dcs_code, /*has_args=*/0, t->compressed, NULL, 0,
        &t->emit_buf);
    if (YETTY_IS_OK(frame_res)) {
        frame_res = yetty_ywire_wire_statemachine_write(t->sm, t->outbuf.data, t->outbuf.size);
    }
    if (YETTY_IS_OK(frame_res)) {
        frame_res = yetty_ywire_wire_statemachine_finish_write(t->sm);
    }
    yetty_ycore_buffer_clear(&t->outbuf);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, frame_res, "dcs_flush_outbuf: frame");

    size_t off = 0;
    while (off < t->emit_buf.size) {
        ptrdiff_t written = write(t->write_fd, t->emit_buf.data + off, t->emit_buf.size - off);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
#ifndef _WIN32
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* The write fd is often O_NONBLOCK behind the caller's back:
                 * raw-mode setup flips the flag on stdin, and stdin/stdout
                 * usually share one open file description of the tty. A
                 * large envelope (a full ImGui frame is ~30 KB of base64)
                 * overruns the PTY buffer mid-write; bailing here would
                 * leave HALF an envelope in the stream — the peer's scanner
                 * then swallows the truncated tail plus the next envelopes
                 * as garbage text. Wait for the fd to drain and finish the
                 * envelope. */
                struct pollfd poll_fd = {.fd = t->write_fd, .events = POLLOUT};
                int poll_result = poll(&poll_fd, 1, DCS_SEND_TIMEOUT_MS);
                if (poll_result < 0 && errno != EINTR) {
                    return YETTY_ERR(yetty_ycore_void, "dcs_flush_outbuf: poll failed");
                }
                if (poll_result == 0) {
                    return YETTY_ERR(yetty_ycore_void,
                                     "dcs_flush_outbuf: write stalled (peer not draining)");
                }
                continue;
            }
#endif
            return YETTY_ERR(yetty_ycore_void, "dcs_flush_outbuf: write failed");
        }
        off += (size_t)written;
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_size_result dcs_send(struct yetty_yclass_transport *base,
                                               const void *bytes, size_t len)
{
    struct dcs_transport *t = (struct dcs_transport *)base;
    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    if (!bytes) {
        return YETTY_ERR(yetty_ycore_size, "dcs_send: NULL bytes with len > 0");
    }
    struct yetty_ycore_void_result wr = yetty_ycore_buffer_write(&t->outbuf, bytes, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, wr, "dcs_send: outbuf grow");
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_size_result dcs_recv(struct yetty_yclass_transport *base, void *buf,
                                               size_t max)
{
    struct dcs_transport *t = (struct dcs_transport *)base;

    /* Flush before blocking on read. yrpc's pattern is write*, then
     * read response — without flushing first the request never
     * reaches the wire and we deadlock waiting for a response that
     * was never asked for. */
    {
        struct yetty_ycore_void_result fr = dcs_flush_outbuf(t);
        YETTY_RETURN_IF_ERR(yetty_ycore_size, fr, "dcs_recv: flush outbuf");
    }

    /* Hand out from already-decoded inbuf if we have bytes pending. */
    if (t->inbuf_off < t->inbuf.size) {
        size_t avail = t->inbuf.size - t->inbuf_off;
        size_t n = avail < max ? avail : max;
        memcpy(buf, t->inbuf.data + t->inbuf_off, n);
        t->inbuf_off += n;
        if (t->inbuf_off == t->inbuf.size) {
            yetty_ycore_buffer_clear(&t->inbuf);
            t->inbuf_off = 0;
        }
        return YETTY_OK(yetty_ycore_size, n);
    }

    /* Pump read_fd → SM until the buffered handler delivers something
     * into inbuf. Bytes outside our DCS envelope (terminal text,
     * other DCS codes) are simply skipped by the SM. */
    while (t->inbuf_off == t->inbuf.size) {
        if (t->eof) {
            return YETTY_OK(yetty_ycore_size, 0);
        }
#ifndef _WIN32
        /* Wait for the fd to actually become readable before reading. The
         * caller's tty is typically in raw mode with VMIN=0, where read()
         * returns 0 immediately when no data is buffered — and the peer's
         * reply often lags our request — so a bare read() would spuriously
         * report EOF (the bug that broke every figure producer's attach
         * handshake). poll() blocks until real data arrives; a genuine
         * disconnect shows up as POLLHUP, and the timeout fails closed. */
        {
            struct pollfd poll_fd = {.fd = t->read_fd, .events = POLLIN};
            int poll_result = poll(&poll_fd, 1, DCS_RECV_TIMEOUT_MS);
            if (poll_result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return YETTY_ERR(yetty_ycore_size, "dcs_recv: poll failed");
            }
            if (poll_result == 0) {
                t->eof = 1; /* timed out waiting for the reply */
                return YETTY_OK(yetty_ycore_size, 0);
            }
            /* Pure hangup with no pending data: real disconnect. */
            if ((poll_fd.revents & (POLLHUP | POLLERR | POLLNVAL)) && !(poll_fd.revents & POLLIN)) {
                t->eof = 1;
                return YETTY_OK(yetty_ycore_size, 0);
            }
        }
#endif
        uint8_t raw[4096];
        ptrdiff_t n = read(t->read_fd, raw, sizeof(raw));
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return YETTY_ERR(yetty_ycore_size, "dcs_recv: read failed");
        }
        if (n == 0) {
#ifndef _WIN32
            /* poll() said readable but VMIN=0 handed us nothing yet — loop and
             * wait again rather than mistaking it for EOF. */
            continue;
#else
            t->eof = 1;
            return YETTY_OK(yetty_ycore_size, 0);
#endif
        }
        struct yetty_ycore_void_result fr =
            yetty_ywire_wire_statemachine_feed(t->sm, (const char *)raw, (size_t)n);
        YETTY_RETURN_IF_ERR(yetty_ycore_size, fr, "dcs_recv: sm feed");
        struct yetty_ycore_void_result pr = yetty_ywire_wire_statemachine_process(t->sm);
        YETTY_RETURN_IF_ERR(yetty_ycore_size, pr, "dcs_recv: sm process");
    }

    /* Now there's data — copy it out via the same hand-out path. */
    size_t avail = t->inbuf.size - t->inbuf_off;
    size_t out = avail < max ? avail : max;
    memcpy(buf, t->inbuf.data + t->inbuf_off, out);
    t->inbuf_off += out;
    if (t->inbuf_off == t->inbuf.size) {
        yetty_ycore_buffer_clear(&t->inbuf);
        t->inbuf_off = 0;
    }
    return YETTY_OK(yetty_ycore_size, out);
}

/* One-way calls never recv(), so flush their buffered request envelope here. */
static struct yetty_ycore_void_result dcs_flush(struct yetty_yclass_transport *base)
{
    struct dcs_transport *t = (struct dcs_transport *)base;
    return dcs_flush_outbuf(t);
}

static struct yetty_ycore_void_result dcs_destroy(struct yetty_yclass_transport *base)
{
    if (!base) {
        return YETTY_OK_VOID();
    }
    struct dcs_transport *t = (struct dcs_transport *)base;
    if (t->sm) {
        struct yetty_ycore_void_result r = yetty_ywire_wire_statemachine_destroy(t->sm);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        t->sm = NULL;
    }
    yetty_ycore_buffer_destroy(&t->inbuf);
    yetty_ycore_buffer_destroy(&t->outbuf);
    yetty_ycore_buffer_destroy(&t->emit_buf);
    /* fds are caller-owned. */
    free(t);
    return YETTY_OK_VOID();
}

struct yetty_yclass_transport_ptr_result yetty_yclass_transport_dcs_create(int read_fd,
                                                                           int write_fd,
                                                                           int dcs_code,
                                                                           int compressed)
{
    if (read_fd < 0 || write_fd < 0) {
        return YETTY_ERR(yetty_yclass_transport_ptr, "transport_dcs_create: invalid fd");
    }
    struct dcs_transport *t = calloc(1, sizeof(*t));
    if (!t) {
        return YETTY_ERR(yetty_yclass_transport_ptr, "transport_dcs_create: calloc");
    }
    static const struct yetty_yclass_transport_ops ops = {
        .send = dcs_send,
        .recv = dcs_recv,
        .destroy = dcs_destroy,
        .flush = dcs_flush,
    };
    t->base.ops = &ops;
    t->read_fd = read_fd;
    t->write_fd = write_fd;
    t->dcs_code = dcs_code;
    t->compressed = compressed ? 1 : 0;

    /* NULL pty — we push bytes via wire_statemachine_feed instead of
     * letting the SM pull them itself. */
    struct yetty_ywire_wire_statemachine_ptr_result sr = yetty_ywire_wire_statemachine_create(NULL);
    if (YETTY_IS_ERR(sr)) {
        free(t);
        return YETTY_ERR(yetty_yclass_transport_ptr, "transport_dcs_create: sm_create", sr);
    }
    t->sm = sr.value;

    struct yetty_ycore_void_result rr = yetty_ywire_wire_statemachine_register_buffered(
        t->sm, YETTY_YWIRE_ENVELOPE_DCS, dcs_code, /*has_args=*/0, dcs_on_envelope, t);
    if (YETTY_IS_ERR(rr)) {
        struct yetty_ycore_void_result dr = yetty_ywire_wire_statemachine_destroy(t->sm);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        free(t);
        return YETTY_ERR(yetty_yclass_transport_ptr, "transport_dcs_create: sm register", rr);
    }
    return YETTY_OK(yetty_yclass_transport_ptr, &t->base);
}
