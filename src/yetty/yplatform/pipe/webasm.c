/* WebAssembly platform input pipe — pipe(2)-backed.
 *
 * Earlier this was an in-memory buffer with no fd, and `read_fd`
 * returned -1. That worked for the platform input event pipe (keyboard
 * / mouse / resize events) because main_loop_tick calls
 * `webasm_platform_input_pipe_process()` directly to drain it and
 * dispatch events into the event loop.
 *
 * It DID NOT work for `telnet-pty.c`, which uses the same
 * `yetty_platform_input_pipe_create()` to construct its decoded-output
 * stream. telnet-pty's pipe is wired into the webasm event loop's
 * `register_pty_pipe` path, which polls a real fd via non-blocking
 * read(). With `read_fd = -1`, that path silently no-ops — emitted
 * bytes pile up in the in-memory buffer and never reach the terminal,
 * so a successfully-established telnet connection still rendered an
 * empty terminal.
 *
 * Switching to a real `pipe(2)` (which emscripten's MEMFS supports —
 * iframe-pty.c uses it directly) makes both consumers work uniformly:
 *  - `register_pty_pipe` gets a valid fd to drain.
 *  - `process()` still drains and dispatches events for the
 *    platform-input-pipe use case, but now via read() on the fd
 *    instead of memcpy out of an internal buffer. */

#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yevent/event.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct yetty_yplatform_webasm_platform_input_pipe {
    struct yetty_ycore_xthread_event_pipe base;
    struct yetty_yevent_event_loop *event_loop;
    int read_fd;
    int write_fd;
};

static void webasm_pipe_destroy(struct yetty_ycore_xthread_event_pipe *self);
static struct yetty_ycore_size_result webasm_pipe_write(struct yetty_ycore_xthread_event_pipe *self,
                                                        const void *data, size_t size);
static struct yetty_ycore_size_result webasm_pipe_read(struct yetty_ycore_xthread_event_pipe *self,
                                                       void *data, size_t max_size);
static struct yetty_ycore_int_result webasm_pipe_read_fd(
    const struct yetty_ycore_xthread_event_pipe *self);
static struct yetty_ycore_void_result webasm_pipe_set_event_loop(
    struct yetty_ycore_xthread_event_pipe *self, struct yetty_yevent_event_loop *loop);

static struct yetty_ycore_void_result webasm_pipe_set_nonblocking_write(
    struct yetty_ycore_xthread_event_pipe *self)
{
    /* webasm pipe is an in-memory ring; writes never block. */
    (void)self;
    return YETTY_OK_VOID();
}

static const struct yetty_platform_input_pipe_ops webasm_pipe_ops = {
    .destroy = webasm_pipe_destroy,
    .write = webasm_pipe_write,
    .read = webasm_pipe_read,
    .read_fd = webasm_pipe_read_fd,
    .set_event_loop = webasm_pipe_set_event_loop,
    .set_nonblocking_write = webasm_pipe_set_nonblocking_write,
};

int yetty_yplatform_input_pipe_webasm_platform_input_pipe_has_pending(
    struct yetty_ycore_xthread_event_pipe *self)
{
    /* No cheap way to peek a pipe's queue depth on emscripten. The
     * process() function below loops until read returns EAGAIN, so
     * "has_pending" is semantically "should we even bother trying?"
     * Always return 1 when the pipe is set up; process() bails on
     * empty without dispatching anything. */
    if (!self) {
        return 0;
    }
    return 1;
}

void yetty_yplatform_input_pipe_webasm_platform_input_pipe_process(
    struct yetty_ycore_xthread_event_pipe *self)
{
    struct yetty_yplatform_webasm_platform_input_pipe *pipe;
    struct yetty_yui_event event;
    ssize_t n;

    if (!self) {
        return;
    }
    pipe = container_of(self, struct yetty_yplatform_webasm_platform_input_pipe, base);
    if (!pipe->event_loop || pipe->read_fd < 0) {
        return;
    }

    /* Drain whole events; pipe is non-blocking so a partial / no-data
     * read terminates the loop. We expect writes to be event-aligned. */
    for (;;) {
        n = read(pipe->read_fd, &event, sizeof(event));
        if (n != (ssize_t)sizeof(event)) {
            return;
        }
        ydebug("webasm_pipe: dispatching event type=%d", (int)event.type);
        /* External-callback boundary (void return): absorb by logging.
         * Every keyboard/mouse/RESIZE event flows through here — a
         * dropped error on the initial RESIZE means the first frame is
         * never requested and the canvas stays blank with no message. */
        struct yetty_ycore_int_result dispatch_res =
            pipe->event_loop->ops->dispatch(pipe->event_loop, &event);
        if (YETTY_IS_ERR(dispatch_res)) {
            char chain_buf[512];
            yetty_ycore_error_snprint(chain_buf, sizeof(chain_buf), dispatch_res.error);
            yerror("webasm_pipe: dispatch of event type=%d failed: %s", (int)event.type,
                   chain_buf);
            yetty_ycore_error_destroy(dispatch_res.error);
        }
    }
}

static void webasm_pipe_destroy(struct yetty_ycore_xthread_event_pipe *self)
{
    struct yetty_yplatform_webasm_platform_input_pipe *pipe;

    pipe = container_of(self, struct yetty_yplatform_webasm_platform_input_pipe, base);
    if (pipe->read_fd >= 0) {
        close(pipe->read_fd);
    }
    if (pipe->write_fd >= 0) {
        close(pipe->write_fd);
    }
    free(pipe);
}

static struct yetty_ycore_size_result webasm_pipe_write(struct yetty_ycore_xthread_event_pipe *self,
                                                        const void *data, size_t size)
{
    struct yetty_yplatform_webasm_platform_input_pipe *pipe;
    ssize_t n;

    pipe = container_of(self, struct yetty_yplatform_webasm_platform_input_pipe, base);
    if (size == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    if (pipe->write_fd < 0) {
        return YETTY_ERR(yetty_ycore_size, "pipe write fd not open");
    }
    n = write(pipe->write_fd, data, size);
    if (n < 0) {
        return YETTY_ERR(yetty_ycore_size, "write to pipe failed");
    }
    return YETTY_OK(yetty_ycore_size, (size_t)n);
}

static struct yetty_ycore_size_result webasm_pipe_read(struct yetty_ycore_xthread_event_pipe *self,
                                                       void *data, size_t max_size)
{
    struct yetty_yplatform_webasm_platform_input_pipe *pipe;
    ssize_t n;

    pipe = container_of(self, struct yetty_yplatform_webasm_platform_input_pipe, base);
    if (max_size == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    if (pipe->read_fd < 0) {
        return YETTY_ERR(yetty_ycore_size, "pipe read fd not open");
    }
    n = read(pipe->read_fd, data, max_size);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return YETTY_OK(yetty_ycore_size, 0);
        }
        return YETTY_ERR(yetty_ycore_size, "read from pipe failed");
    }
    return YETTY_OK(yetty_ycore_size, (size_t)n);
}

static struct yetty_ycore_int_result webasm_pipe_read_fd(
    const struct yetty_ycore_xthread_event_pipe *self)
{
    const struct yetty_yplatform_webasm_platform_input_pipe *pipe;

    pipe = container_of(self, struct yetty_yplatform_webasm_platform_input_pipe, base);
    if (pipe->read_fd < 0) {
        return YETTY_ERR(yetty_ycore_int, "pipe read fd not open");
    }
    return YETTY_OK(yetty_ycore_int, pipe->read_fd);
}

static struct yetty_ycore_void_result webasm_pipe_set_event_loop(
    struct yetty_ycore_xthread_event_pipe *self, struct yetty_yevent_event_loop *loop)
{
    struct yetty_yplatform_webasm_platform_input_pipe *pipe;

    pipe = container_of(self, struct yetty_yplatform_webasm_platform_input_pipe, base);
    pipe->event_loop = loop;
    return YETTY_OK_VOID();
}

struct yetty_yplatform_input_pipe_result yetty_platform_input_pipe_create(void)
{
    struct yetty_yplatform_webasm_platform_input_pipe *p;
    int fds[2];
    int flags;

    p = calloc(1, sizeof(*p));
    if (!p) {
        return YETTY_ERR(yetty_yplatform_input_pipe, "failed to allocate webasm input pipe");
    }
    p->base.ops = &webasm_pipe_ops;
    p->read_fd = -1;
    p->write_fd = -1;

    if (pipe(fds) != 0) {
        free(p);
        return YETTY_ERR(yetty_yplatform_input_pipe, "pipe() failed");
    }
    p->read_fd = fds[0];
    p->write_fd = fds[1];

    /* Read end non-blocking so process() / pty-pipe drain never stalls
     * the rAF tick. Write end stays blocking — webasm is single-
     * threaded so writes are tiny and never push back. */
    flags = fcntl(p->read_fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(p->read_fd, F_SETFL, flags | O_NONBLOCK);
    }

    return YETTY_OK(yetty_yplatform_input_pipe, &p->base);
}
