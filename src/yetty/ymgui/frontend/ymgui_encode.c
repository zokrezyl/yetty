/*
 * ymgui_encode.c — non-blocking write helper.
 *
 * The encoder/envelope/compression all live in yetty_yface now. What's
 * left is the at-most-one-in-flight queue that lets the demo loop's
 * write() never block on PTY backpressure. The C type lives outside any
 * imgui dep so the C++ shim and the C-only callers can share it.
 */

#include "ymgui_encode.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
typedef long long ssize_t;
#define YMGUI_WRITE _write
#else
#include <poll.h>
#include <unistd.h>
#define YMGUI_WRITE write
#endif

struct yetty_ymgui_pending {
    uint8_t *data;
    size_t size;
    size_t off;
};
static struct yetty_ymgui_pending g_pending = {NULL, 0, 0};

static int try_drain_pending(int fd)
{
    while (g_pending.off < g_pending.size) {
        ssize_t w = YMGUI_WRITE(fd, (const char *)g_pending.data + g_pending.off,
                                (unsigned)(g_pending.size - g_pending.off));
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 1;
            }
            return -1;
        }
        g_pending.off += (size_t)w;
    }
    free(g_pending.data);
    g_pending.data = NULL;
    g_pending.size = g_pending.off = 0;
    return 0;
}

int yetty_ymgui_pending_flush(int fd)
{
    if (!g_pending.data) {
        return 0;
    }
    return try_drain_pending(fd);
}

int yetty_ymgui_pending_active(void)
{
    return g_pending.data != NULL;
}

int yetty_ymgui_pending_drain_blocking(int fd)
{
#ifdef _WIN32
    /* Windows path is rarely exercised; rely on the kernel buffer being
     * big enough and the existing non-blocking flush. */
    return ymgui_pending_flush(fd);
#else
    while (g_pending.data) {
        int r = try_drain_pending(fd);
        if (r < 0) {
            return -1;
        }
        if (r == 0) {
            return 0;
        }
        struct pollfd pfd = {.fd = fd, .events = POLLOUT, .revents = 0};
        int n = poll(&pfd, 1, 5000 /* ms */);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            return -1; /* timed out — receiver wedged */
        }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            return -1;
        }
    }
    return 0;
#endif
}

int yetty_ymgui_pending_write(int fd, const uint8_t *bytes, size_t len)
{
    /* Drop new emit if previous one is still in flight — interleaving
     * would corrupt the OSC stream. */
    if (g_pending.data) {
        int r = try_drain_pending(fd);
        if (r < 0) {
            return -1;
        }
        if (r > 0) {
            return 1;
        }
    }
    if (len == 0) {
        return 0;
    }

    size_t off = 0;
    while (off < len) {
        ssize_t w = YMGUI_WRITE(fd, (const char *)(bytes + off), (unsigned)(len - off));
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                /* Park the unsent tail in the queue. */
                size_t left = len - off;
                uint8_t *tail = (uint8_t *)malloc(left);
                if (!tail) {
                    return -1;
                }
                memcpy(tail, bytes + off, left);
                g_pending.data = tail;
                g_pending.size = left;
                g_pending.off = 0;
                return 0;
            }
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}
