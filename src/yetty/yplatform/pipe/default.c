/* Unix platform input pipe implementation */

/* Need _GNU_SOURCE for F_SETPIPE_SZ — it lives behind the __USE_GNU
 * gate in glibc's <bits/fcntl-linux.h>. Define BEFORE any system
 * header so the macro is in scope when fcntl.h is included. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/ycore/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Unix input pipe - embeds base as first member */
struct yetty_yplatform_unix_platform_input_pipe {
    struct yetty_ycore_xthread_event_pipe base;
    int read_fd;
    int write_fd;
};

/* Forward declarations */
static void unix_pipe_destroy(struct yetty_ycore_xthread_event_pipe *self);
static struct yetty_ycore_size_result unix_pipe_write(struct yetty_ycore_xthread_event_pipe *self,
                                                      const void *data, size_t size);
static struct yetty_ycore_size_result unix_pipe_read(struct yetty_ycore_xthread_event_pipe *self,
                                                     void *data, size_t max_size);
static struct yetty_ycore_int_result unix_pipe_read_fd(
    const struct yetty_ycore_xthread_event_pipe *self);
static struct yetty_ycore_void_result unix_pipe_set_event_loop(
    struct yetty_ycore_xthread_event_pipe *self, struct yetty_yevent_event_loop *loop);
static struct yetty_ycore_void_result unix_pipe_set_nonblocking_write(
    struct yetty_ycore_xthread_event_pipe *self);

/* Ops table */
static const struct yetty_platform_input_pipe_ops unix_pipe_ops = {
    .destroy = unix_pipe_destroy,
    .write = unix_pipe_write,
    .read = unix_pipe_read,
    .read_fd = unix_pipe_read_fd,
    .set_event_loop = unix_pipe_set_event_loop,
    .set_nonblocking_write = unix_pipe_set_nonblocking_write,
};

/* Implementation */

static void unix_pipe_destroy(struct yetty_ycore_xthread_event_pipe *self)
{
    struct yetty_yplatform_unix_platform_input_pipe *pipe_impl;

    pipe_impl = container_of(self, struct yetty_yplatform_unix_platform_input_pipe, base);

    if (pipe_impl->read_fd >= 0) {
        close(pipe_impl->read_fd);
    }
    if (pipe_impl->write_fd >= 0) {
        close(pipe_impl->write_fd);
    }

    free(pipe_impl);
}

static struct yetty_ycore_size_result unix_pipe_write(struct yetty_ycore_xthread_event_pipe *self,
                                                      const void *data, size_t size)
{
    struct yetty_yplatform_unix_platform_input_pipe *pipe_impl =
        container_of(self, struct yetty_yplatform_unix_platform_input_pipe, base);
    ssize_t written;

    if (pipe_impl->write_fd < 0) {
        return YETTY_ERR(yetty_ycore_size, "pipe write fd not open");
    }

    if (size == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    written = write(pipe_impl->write_fd, data, size);
    if (written < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            /* Non-blocking writer: kernel pipe is full. The caller must
             * keep the unwritten bytes around and retry once the reader
             * drains the pipe. */
            return YETTY_OK(yetty_ycore_size, 0);
        }
        return YETTY_ERR(yetty_ycore_size, "write to pipe failed");
    }

    return YETTY_OK(yetty_ycore_size, (size_t)written);
}

static struct yetty_ycore_size_result unix_pipe_read(struct yetty_ycore_xthread_event_pipe *self,
                                                     void *data, size_t max_size)
{
    struct yetty_yplatform_unix_platform_input_pipe *pipe_impl;
    ssize_t bytes_read;

    pipe_impl = container_of(self, struct yetty_yplatform_unix_platform_input_pipe, base);

    if (pipe_impl->read_fd < 0) {
        return YETTY_ERR(yetty_ycore_size, "pipe read fd not open");
    }

    if (max_size == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    bytes_read = read(pipe_impl->read_fd, data, max_size);
    if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return YETTY_OK(yetty_ycore_size, 0);
        }
        return YETTY_ERR(yetty_ycore_size, "read from pipe failed");
    }

    return YETTY_OK(yetty_ycore_size, (size_t)bytes_read);
}

static struct yetty_ycore_int_result unix_pipe_read_fd(
    const struct yetty_ycore_xthread_event_pipe *self)
{
    const struct yetty_yplatform_unix_platform_input_pipe *pipe_impl;

    pipe_impl = container_of(self, struct yetty_yplatform_unix_platform_input_pipe, base);

    if (pipe_impl->read_fd < 0) {
        return YETTY_ERR(yetty_ycore_int, "pipe read fd not open");
    }

    return YETTY_OK(yetty_ycore_int, pipe_impl->read_fd);
}

static struct yetty_ycore_void_result unix_pipe_set_event_loop(
    struct yetty_ycore_xthread_event_pipe *self, struct yetty_yevent_event_loop *loop)
{
    /* No-op on Unix - EventLoop polls the fd directly */
    (void)self;
    (void)loop;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result unix_pipe_set_nonblocking_write(
    struct yetty_ycore_xthread_event_pipe *self)
{
    struct yetty_yplatform_unix_platform_input_pipe *pipe_impl =
        container_of(self, struct yetty_yplatform_unix_platform_input_pipe, base);
    if (pipe_impl->write_fd < 0) {
        return YETTY_ERR(yetty_ycore_void, "unix_pipe_set_nonblocking_write: write fd not open");
    }
    int flags = fcntl(pipe_impl->write_fd, F_GETFL, 0);
    if (flags < 0) {
        return YETTY_ERR(yetty_ycore_void, "unix_pipe_set_nonblocking_write: F_GETFL failed");
    }
    if (fcntl(pipe_impl->write_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return YETTY_ERR(yetty_ycore_void,
                         "unix_pipe_set_nonblocking_write: F_SETFL O_NONBLOCK failed");
    }
    return YETTY_OK_VOID();
}

/* Create function */

struct yetty_yplatform_input_pipe_result yetty_platform_input_pipe_create(void)
{
    struct yetty_yplatform_unix_platform_input_pipe *pipe_impl;
    int fds[2];
    int flags;

    pipe_impl = malloc(sizeof(struct yetty_yplatform_unix_platform_input_pipe));
    if (!pipe_impl) {
        return YETTY_ERR(yetty_yplatform_input_pipe, "failed to allocate input pipe");
    }

    pipe_impl->base.ops = &unix_pipe_ops;
    pipe_impl->read_fd = -1;
    pipe_impl->write_fd = -1;

    if (pipe(fds) != 0) {
        free(pipe_impl);
        return YETTY_ERR(yetty_yplatform_input_pipe, "pipe() failed");
    }

    pipe_impl->read_fd = fds[0];
    pipe_impl->write_fd = fds[1];

    /* Set read end non-blocking */
    flags = fcntl(pipe_impl->read_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(pipe_impl->read_fd, F_SETFL, flags | O_NONBLOCK);
    }

    /* Grow the kernel buffer to the per-user max. The default 64 KiB
     * gets blown out by telnet_emit_byte streaming a SCENE_BIN payload
     * for a logo (~60–220 KiB after b64) or any larger render — the
     * blocking 1-byte writes happen on the libuv loop thread, the
     * draining consumer (wire-sm coro) only runs between libuv I/O
     * callbacks, and once the pipe fills the loop deadlocks. Read
     * /proc/sys/fs/pipe-max-size for the max we're allowed (typically
     * 1 MiB on Linux). Fall back to 1 MiB if /proc isn't readable.
     * Failures here are non-fatal — the pipe still works at the
     * default size; only large bursts will hit the old deadlock. */
#ifdef F_SETPIPE_SZ
    {
        int pipe_max = 1 << 20;
        FILE *mf = fopen("/proc/sys/fs/pipe-max-size", "r");
        if (mf) {
            (void)fscanf(mf, "%d", &pipe_max);
            fclose(mf);
        }
        if (pipe_max < (1 << 17)) {
            pipe_max = 1 << 17;
        }
        (void)fcntl(pipe_impl->write_fd, F_SETPIPE_SZ, pipe_max);
    }
#endif

    return YETTY_OK(yetty_yplatform_input_pipe, &pipe_impl->base);
}
