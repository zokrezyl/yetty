/* yclass pty transport — sole owner of a PTY fd pair (see transport-pty.h).
 *
 * Owns fd_in/fd_out, the terminal raw-mode, and an ordered non-blocking
 * outbound queue. The base yetty_yclass_transport vtable maps send→queue,
 * recv→recv_blocking, flush→flush_blocking; the richer reactor seam
 * (fd / read_available / pump_writable / want_write) is the concrete API the
 * yetty_ywire_connection multiplexer drives from a host loop.
 */

#include <yetty/yclass/transport-pty.h>

#include <yetty/yclass/transport-reactor.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h> /* _read / _write — MSVC ships no <unistd.h> */
#else
#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>
#endif

/* How long recv_blocking() waits for inbound bytes before declaring the link
 * dead. Reached only for the synchronous attach handshake (translate_class +
 * GET_ROOT); steady-state figure traffic is one-way and never recv()s. A real
 * host answers in well under a second; the generous bound just keeps a
 * host-less invocation from hanging forever. */
#define PTY_RECV_TIMEOUT_MS 5000

struct yetty_yclass_transport_pty {
    struct yetty_yclass_transport base;
    int fd_in;
    int fd_out;
    int eof;

    /* Raw-mode / O_NONBLOCK ownership, restored at destroy. */
    int raw_active;
    int flags_saved;
#ifndef _WIN32
    struct termios saved_tty;
#endif
    int fd_in_flags;
    int fd_out_flags;

    /* Ordered outbound queue: bytes in [out_off, size) await the wire. */
    struct yetty_ycore_buffer out_queue;
    size_t out_off;
};

/*===========================================================================
 * Outbound queue
 *=========================================================================*/

struct yetty_ycore_void_result yetty_yclass_transport_pty_queue(
    struct yetty_yclass_transport_pty *transport, const void *bytes, size_t len)
{
    if (!transport) {
        return YETTY_ERR(yetty_ycore_void, "transport_pty_queue: NULL transport");
    }
    if (len == 0) {
        return YETTY_OK_VOID();
    }
    if (!bytes) {
        return YETTY_ERR(yetty_ycore_void, "transport_pty_queue: NULL bytes with len > 0");
    }
    /* Compact consumed prefix so the queue stays bounded across many frames. */
    if (transport->out_off > 0) {
        if (transport->out_off >= transport->out_queue.size) {
            yetty_ycore_buffer_clear(&transport->out_queue);
        } else {
            memmove(transport->out_queue.data, transport->out_queue.data + transport->out_off,
                    transport->out_queue.size - transport->out_off);
            transport->out_queue.size -= transport->out_off;
        }
        transport->out_off = 0;
    }
    struct yetty_ycore_void_result wr = yetty_ycore_buffer_write(&transport->out_queue, bytes, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "transport_pty_queue: out_queue grow");
    return YETTY_OK_VOID();
}

struct yetty_ycore_size_result yetty_yclass_transport_pty_pump_writable(
    struct yetty_yclass_transport_pty *transport)
{
    if (!transport) {
        return YETTY_ERR(yetty_ycore_size, "transport_pty_pump_writable: NULL transport");
    }
    size_t written = 0;
    while (transport->out_off < transport->out_queue.size) {
        const void *src = transport->out_queue.data + transport->out_off;
        size_t remaining = transport->out_queue.size - transport->out_off;
#ifdef _WIN32
        int n = _write(transport->fd_out, src, (unsigned)remaining);
#else
        ptrdiff_t n = write(transport->fd_out, src, remaining);
#endif
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; /* would block — leave the rest queued, caller re-arms */
            }
            return YETTY_ERR(yetty_ycore_size, "transport_pty_pump_writable: write failed");
        }
        transport->out_off += (size_t)n;
        written += (size_t)n;
    }
    if (transport->out_off >= transport->out_queue.size) {
        yetty_ycore_buffer_clear(&transport->out_queue);
        transport->out_off = 0;
    }
    return YETTY_OK(yetty_ycore_size, written);
}

struct yetty_ycore_void_result yetty_yclass_transport_pty_flush_blocking(
    struct yetty_yclass_transport_pty *transport)
{
    if (!transport) {
        return YETTY_ERR(yetty_ycore_void, "transport_pty_flush_blocking: NULL transport");
    }
    while (transport->out_off < transport->out_queue.size) {
        const void *src = transport->out_queue.data + transport->out_off;
        size_t remaining = transport->out_queue.size - transport->out_off;
#ifdef _WIN32
        int n = _write(transport->fd_out, src, (unsigned)remaining);
#else
        ptrdiff_t n = write(transport->fd_out, src, remaining);
#endif
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
#ifndef _WIN32
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd poll_fd = {.fd = transport->fd_out, .events = POLLOUT};
                poll(&poll_fd, 1, -1);
                continue;
            }
#endif
            return YETTY_ERR(yetty_ycore_void, "transport_pty_flush_blocking: write failed");
        }
        transport->out_off += (size_t)n;
    }
    yetty_ycore_buffer_clear(&transport->out_queue);
    transport->out_off = 0;
    return YETTY_OK_VOID();
}

int yetty_yclass_transport_pty_want_write(struct yetty_yclass_transport_pty *transport)
{
    return transport && transport->out_off < transport->out_queue.size;
}

int yetty_yclass_transport_pty_is_eof(struct yetty_yclass_transport_pty *transport)
{
    return transport ? transport->eof : 1;
}

int yetty_yclass_transport_pty_fd(struct yetty_yclass_transport_pty *transport)
{
    return transport ? transport->fd_in : -1;
}

int yetty_yclass_transport_pty_out_fd(struct yetty_yclass_transport_pty *transport)
{
    return transport ? transport->fd_out : -1;
}

/*===========================================================================
 * Inbound
 *=========================================================================*/

struct yetty_ycore_size_result yetty_yclass_transport_pty_read_available(
    struct yetty_yclass_transport_pty *transport, void *buf, size_t max)
{
    if (!transport || !buf) {
        return YETTY_ERR(yetty_ycore_size, "transport_pty_read_available: NULL arg");
    }
    if (max == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
#ifdef _WIN32
    int n = _read(transport->fd_in, buf, (unsigned)max);
#else
    ptrdiff_t n = read(transport->fd_in, buf, max);
#endif
    if (n < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            return YETTY_OK(yetty_ycore_size, 0);
        }
        return YETTY_ERR(yetty_ycore_size, "transport_pty_read_available: read failed");
    }
    if (n == 0) {
        /* On a raw tty (VMIN=0) a 0-byte read just means "nothing buffered yet",
         * not EOF. Only a non-tty fd (pipe) signals real end-of-file with 0. */
#ifndef _WIN32
        if (!isatty(transport->fd_in)) {
            transport->eof = 1;
        }
#else
        transport->eof = 1;
#endif
        return YETTY_OK(yetty_ycore_size, 0);
    }
    return YETTY_OK(yetty_ycore_size, (size_t)n);
}

struct yetty_ycore_size_result yetty_yclass_transport_pty_recv_blocking(
    struct yetty_yclass_transport_pty *transport, void *buf, size_t max)
{
    if (!transport || !buf) {
        return YETTY_ERR(yetty_ycore_size, "transport_pty_recv_blocking: NULL arg");
    }
    if (max == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    for (;;) {
        if (transport->eof) {
            return YETTY_OK(yetty_ycore_size, 0);
        }
#ifndef _WIN32
        struct pollfd poll_fd = {.fd = transport->fd_in, .events = POLLIN};
        int poll_result = poll(&poll_fd, 1, PTY_RECV_TIMEOUT_MS);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return YETTY_ERR(yetty_ycore_size, "transport_pty_recv_blocking: poll failed");
        }
        if (poll_result == 0) {
            transport->eof = 1; /* timed out waiting for the peer */
            return YETTY_OK(yetty_ycore_size, 0);
        }
        if ((poll_fd.revents & (POLLHUP | POLLERR | POLLNVAL)) && !(poll_fd.revents & POLLIN)) {
            transport->eof = 1;
            return YETTY_OK(yetty_ycore_size, 0);
        }
        ptrdiff_t n = read(transport->fd_in, buf, max);
#else
        int n = _read(transport->fd_in, buf, (unsigned)max);
#endif
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            return YETTY_ERR(yetty_ycore_size, "transport_pty_recv_blocking: read failed");
        }
        if (n == 0) {
#ifndef _WIN32
            continue; /* raw tty VMIN=0 handed us nothing yet — keep waiting */
#else
            transport->eof = 1;
            return YETTY_OK(yetty_ycore_size, 0);
#endif
        }
        return YETTY_OK(yetty_ycore_size, (size_t)n);
    }
}

/*===========================================================================
 * Raw mode
 *=========================================================================*/

struct yetty_ycore_void_result yetty_yclass_transport_pty_enable_raw_mode(
    struct yetty_yclass_transport_pty *transport)
{
    if (!transport) {
        return YETTY_ERR(yetty_ycore_void, "transport_pty_enable_raw_mode: NULL transport");
    }
    if (transport->raw_active) {
        return YETTY_OK_VOID();
    }
#ifdef _WIN32
    (void)transport;
    return YETTY_OK_VOID();
#else
    /* Save fd flags so destroy can restore O_NONBLOCK exactly. */
    transport->fd_in_flags = fcntl(transport->fd_in, F_GETFL, 0);
    transport->fd_out_flags = fcntl(transport->fd_out, F_GETFL, 0);
    transport->flags_saved = 1;

    if (isatty(transport->fd_in)) {
        /* Raw mode breaks the cooked-tty echo loop: the slave echoes the OSC
         * bytes we write back, which would be re-read and parsed as garbage. */
        if (tcgetattr(transport->fd_in, &transport->saved_tty) == 0) {
            struct termios raw = transport->saved_tty;
            cfmakeraw(&raw);
            raw.c_cc[VMIN] = 0;
            raw.c_cc[VTIME] = 0;
            if (tcsetattr(transport->fd_in, TCSANOW, &raw) == 0) {
                transport->raw_active = 1;
            }
        }
    } else if (transport->fd_in_flags >= 0) {
        /* Non-tty (pipe): make reads non-blocking so read_available never
         * stalls the loop after the kernel buffer drains. */
        fcntl(transport->fd_in, F_SETFL, transport->fd_in_flags | O_NONBLOCK);
    }
    if (transport->fd_out_flags >= 0) {
        fcntl(transport->fd_out, F_SETFL, transport->fd_out_flags | O_NONBLOCK);
    }
    return YETTY_OK_VOID();
#endif
}

/*===========================================================================
 * Base vtable adapters + lifecycle
 *=========================================================================*/

static struct yetty_ycore_size_result pty_base_send(struct yetty_yclass_transport *base,
                                                    const void *bytes, size_t len)
{
    struct yetty_yclass_transport_pty *transport = (struct yetty_yclass_transport_pty *)base;
    struct yetty_ycore_void_result qr = yetty_yclass_transport_pty_queue(transport, bytes, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_size, qr, "transport_pty send: queue");
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_size_result pty_base_recv(struct yetty_yclass_transport *base, void *buf,
                                                    size_t max)
{
    return yetty_yclass_transport_pty_recv_blocking((struct yetty_yclass_transport_pty *)base, buf,
                                                    max);
}

static struct yetty_ycore_void_result pty_base_flush(struct yetty_yclass_transport *base)
{
    return yetty_yclass_transport_pty_flush_blocking((struct yetty_yclass_transport_pty *)base);
}

static struct yetty_ycore_void_result pty_base_destroy(struct yetty_yclass_transport *base)
{
    return yetty_yclass_transport_pty_destroy((struct yetty_yclass_transport_pty *)base);
}

struct yetty_yclass_transport *yetty_yclass_transport_pty_base(
    struct yetty_yclass_transport_pty *transport)
{
    return transport ? &transport->base : NULL;
}

/*===========================================================================
 * Reactor capability view (transport-reactor.h)
 *
 * A SEPARATE interface from the byte-stream vtable: the loop-drivable fd-owner
 * ops the yetty_ywire_connection multiplexer rides, so ywire stays abstract over
 * the pty and never links yclass. Each op takes the concrete transport as void*
 * userdata (the void* arg implicitly converts to the typed pointer the concrete
 * function expects). */
static int pty_reactor_fd(void *transport)
{
    return yetty_yclass_transport_pty_fd(transport);
}

static int pty_reactor_out_fd(void *transport)
{
    return yetty_yclass_transport_pty_out_fd(transport);
}

static int pty_reactor_want_write(void *transport)
{
    return yetty_yclass_transport_pty_want_write(transport);
}

static int pty_reactor_is_eof(void *transport)
{
    return yetty_yclass_transport_pty_is_eof(transport);
}

static struct yetty_ycore_size_result pty_reactor_read_available(void *transport, void *buf,
                                                                 size_t max)
{
    return yetty_yclass_transport_pty_read_available(transport, buf, max);
}

static struct yetty_ycore_void_result pty_reactor_queue(void *transport, const void *bytes,
                                                        size_t len)
{
    return yetty_yclass_transport_pty_queue(transport, bytes, len);
}

static struct yetty_ycore_size_result pty_reactor_pump_writable(void *transport)
{
    return yetty_yclass_transport_pty_pump_writable(transport);
}

static struct yetty_ycore_void_result pty_reactor_enable_raw_mode(void *transport)
{
    return yetty_yclass_transport_pty_enable_raw_mode(transport);
}

struct yetty_yclass_transport_reactor yetty_yclass_transport_pty_reactor(
    struct yetty_yclass_transport_pty *transport)
{
    /* Constant ops table — program-lifetime storage as a function-local static
     * const, so no file-scope symbol. */
    static const struct yetty_yclass_transport_reactor_ops ops = {
        .fd = pty_reactor_fd,
        .out_fd = pty_reactor_out_fd,
        .want_write = pty_reactor_want_write,
        .is_eof = pty_reactor_is_eof,
        .read_available = pty_reactor_read_available,
        .queue = pty_reactor_queue,
        .pump_writable = pty_reactor_pump_writable,
        .enable_raw_mode = pty_reactor_enable_raw_mode,
    };
    return (struct yetty_yclass_transport_reactor){.userdata = transport, .ops = &ops};
}

struct yetty_ycore_void_result yetty_yclass_transport_pty_destroy(
    struct yetty_yclass_transport_pty *transport)
{
    if (!transport) {
        return YETTY_OK_VOID();
    }
#ifndef _WIN32
    if (transport->raw_active) {
        tcsetattr(transport->fd_in, TCSANOW, &transport->saved_tty);
        transport->raw_active = 0;
    }
    if (transport->flags_saved) {
        if (transport->fd_in_flags >= 0) {
            fcntl(transport->fd_in, F_SETFL, transport->fd_in_flags);
        }
        if (transport->fd_out_flags >= 0) {
            fcntl(transport->fd_out, F_SETFL, transport->fd_out_flags);
        }
        transport->flags_saved = 0;
    }
#endif
    yetty_ycore_buffer_destroy(&transport->out_queue);
    /* fds are borrowed — not closed here. */
    free(transport);
    return YETTY_OK_VOID();
}

struct yetty_yclass_transport_pty_ptr_result yetty_yclass_transport_pty_create(int fd_in,
                                                                               int fd_out)
{
    if (fd_in < 0 || fd_out < 0) {
        return YETTY_ERR(yetty_yclass_transport_pty_ptr, "transport_pty_create: invalid fd");
    }
    struct yetty_yclass_transport_pty *transport = calloc(1, sizeof(*transport));
    if (!transport) {
        return YETTY_ERR(yetty_yclass_transport_pty_ptr, "transport_pty_create: calloc");
    }
    /* Constant vtable — program-lifetime storage as a function-local static
     * const, so no file-scope symbol. */
    static const struct yetty_yclass_transport_ops ops = {
        .send = pty_base_send,
        .recv = pty_base_recv,
        .destroy = pty_base_destroy,
        .flush = pty_base_flush,
    };
    transport->base.ops = &ops;
    transport->fd_in = fd_in;
    transport->fd_out = fd_out;
    transport->fd_in_flags = -1;
    transport->fd_out_flags = -1;
    return YETTY_OK(yetty_yclass_transport_pty_ptr, transport);
}
