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
#include <unistd.h>
#endif

struct dcs_transport {
    struct yetty_yclass_transport base;
    int read_fd;
    int write_fd;
    int dcs_code;
    int compressed;

    /* Outbound: yrpc send() calls accumulate here; flushed as one
     * envelope on the next recv(). */
    struct yetty_ycore_buffer outbuf;

    /* Inbound: wire_statemachine driven from `read_fd`; the buffered
     * handler appends decoded envelope bodies to `inbuf` and recv()
     * hands bytes out from `inbuf_off`. */
    struct yetty_ywire_wire_statemachine *sm;
    struct yetty_ycore_buffer inbuf;
    size_t inbuf_off;
    int eof;
};

/* Buffered handler — fires once per inbound envelope with the full
 * decoded body. Append it to inbuf for the next recv() call. */
static struct yetty_ycore_void_result dcs_on_envelope(void *userdata,
                                                       enum yetty_ywire_envelope_kind kind,
                                                       int code, const uint8_t *args,
                                                       size_t args_len, const uint8_t *payload,
                                                       size_t payload_len)
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
    struct yetty_ycore_void_result r =
        yetty_ywire_emit_to_fd(t->write_fd, YETTY_YWIRE_ENVELOPE_DCS, t->dcs_code,
                               /*has_args=*/0, t->compressed, NULL, 0, t->outbuf.data,
                               t->outbuf.size);
    yetty_ycore_buffer_clear(&t->outbuf);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "dcs_flush_outbuf: emit_to_fd");
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
        uint8_t raw[4096];
        ptrdiff_t n = read(t->read_fd, raw, sizeof(raw));
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return YETTY_ERR(yetty_ycore_size, "dcs_recv: read failed");
        }
        if (n == 0) {
            t->eof = 1;
            return YETTY_OK(yetty_ycore_size, 0);
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
    };
    t->base.ops = &ops;
    t->read_fd = read_fd;
    t->write_fd = write_fd;
    t->dcs_code = dcs_code;
    t->compressed = compressed ? 1 : 0;

    /* NULL pty — we push bytes via wire_statemachine_feed instead of
     * letting the SM pull them itself. */
    struct yetty_ywire_wire_statemachine_ptr_result sr =
        yetty_ywire_wire_statemachine_create(NULL);
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
