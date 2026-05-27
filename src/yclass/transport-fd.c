/* fd transport — bidirectional byte stream over read(2) / write(2). */

#include <yclass/transport-fd.h>

#include <yetty/ycore/result.h>

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

struct fd_transport {
    struct yetty_yclass_transport base;
    int fd;
};

static struct yetty_ycore_size_result fd_send(struct yetty_yclass_transport *t, const void *bytes,
                                              size_t len)
{
    struct fd_transport *ft = (struct fd_transport *)t;
    for (;;) {
        ssize_t w = write(ft->fd, bytes, len);
        if (w >= 0)
            return YETTY_OK(yetty_ycore_size, (size_t)w);
        if (errno == EINTR)
            continue;
        return YETTY_ERR(yetty_ycore_size, "yetty_yclass_transport_fd: write failed");
    }
}

static struct yetty_ycore_size_result fd_recv(struct yetty_yclass_transport *t, void *buf,
                                              size_t max)
{
    struct fd_transport *ft = (struct fd_transport *)t;
    for (;;) {
        ssize_t r = read(ft->fd, buf, max);
        if (r >= 0)
            return YETTY_OK(yetty_ycore_size, (size_t)r);
        if (errno == EINTR)
            continue;
        return YETTY_ERR(yetty_ycore_size, "yetty_yclass_transport_fd: read failed");
    }
}

static void fd_destroy(struct yetty_yclass_transport *t)
{
    struct fd_transport *ft = (struct fd_transport *)t;
    if (ft->fd >= 0)
        close(ft->fd);
    free(ft);
}

struct yetty_yclass_transport_ptr_result yetty_yclass_transport_fd_create(int fd)
{
    if (fd < 0)
        return YETTY_ERR(yetty_yclass_transport_ptr,
                         "yetty_yclass_transport_fd_create: negative fd");
    struct fd_transport *ft = calloc(1, sizeof(*ft));
    if (!ft)
        return YETTY_ERR(yetty_yclass_transport_ptr,
                         "yetty_yclass_transport_fd_create: calloc failed");
    static const struct yetty_yclass_transport_ops ops = {
        .send = fd_send,
        .recv = fd_recv,
        .destroy = fd_destroy,
    };
    ft->base.ops = &ops;
    ft->fd = fd;
    return YETTY_OK(yetty_yclass_transport_ptr, &ft->base);
}
