/* WebAssembly platform input pipe implementation */

#include <yetty/platform/platform-input-pipe.h>
#include <yetty/ycore/event-loop.h>
#include <yetty/ycore/event.h>
#include <yetty/ycore/types.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

#define PIPE_BUFFER_CAPACITY 16384

/* WebASM input pipe - uses an internal buffer instead of fd pipe */
struct yetty_yplatform_webasm_platform_input_pipe {
    struct yetty_ycore_xthread_event_pipe base;
    struct yetty_yplatform_event_loop *event_loop;
    char *buffer;
    size_t buffer_size;
    size_t buffer_capacity;
    int callback_pending;
};

/* Forward declarations */
static void webasm_pipe_destroy(struct yetty_ycore_xthread_event_pipe *self);
static struct yetty_ycore_size_result webasm_pipe_write(struct yetty_ycore_xthread_event_pipe *self,
                                                        const void *data, size_t size);
static struct yetty_ycore_size_result webasm_pipe_read(struct yetty_ycore_xthread_event_pipe *self,
                                                       void *data, size_t max_size);
static struct yetty_ycore_int_result webasm_pipe_read_fd(
    const struct yetty_ycore_xthread_event_pipe *self);
static struct yetty_ycore_void_result webasm_pipe_set_event_loop(
    struct yetty_ycore_xthread_event_pipe *self, struct yetty_yplatform_event_loop *loop);

/* Ops table */
static const struct yetty_platform_input_pipe_ops webasm_pipe_ops = {
    .destroy = webasm_pipe_destroy,
    .write = webasm_pipe_write,
    .read = webasm_pipe_read,
    .read_fd = webasm_pipe_read_fd,
    .set_event_loop = webasm_pipe_set_event_loop,
};

/* Check if pipe has pending events - called from main loop tick */
int yetty_yplatform_input_pipe_webasm_platform_input_pipe_has_pending(struct yetty_ycore_xthread_event_pipe *self)
{
    struct yetty_yplatform_webasm_platform_input_pipe *pipe;

    if (!self) {
        return 0;
    }

    pipe = container_of(self, struct yetty_yplatform_webasm_platform_input_pipe, base);
    return pipe->buffer_size >= sizeof(struct yetty_yui_event);
}

/* Process pending events from main loop - avoids Asyncify issues */
void yetty_yplatform_input_pipe_webasm_platform_input_pipe_process(struct yetty_ycore_xthread_event_pipe *self)
{
    struct yetty_yplatform_webasm_platform_input_pipe *pipe;
    struct yetty_yui_event event;

    if (!self) {
        return;
    }

    pipe = container_of(self, struct yetty_yplatform_webasm_platform_input_pipe, base);

    if (!pipe->event_loop) {
        return;
    }

    /* Process all pending events */
    while (pipe->buffer_size >= sizeof(event)) {
        memcpy(&event, pipe->buffer, sizeof(event));
        memmove(pipe->buffer, pipe->buffer + sizeof(event), pipe->buffer_size - sizeof(event));
        pipe->buffer_size -= sizeof(event);

        ydebug("webasm_pipe: dispatching event type=%d", (int)event.type);
        pipe->event_loop->ops->dispatch(pipe->event_loop, &event);
    }

    pipe->callback_pending = 0;
}

/* Implementation */

static void webasm_pipe_destroy(struct yetty_ycore_xthread_event_pipe *self)
{
    struct yetty_yplatform_webasm_platform_input_pipe *pipe;

    pipe = container_of(self, struct yetty_yplatform_webasm_platform_input_pipe, base);
    free(pipe->buffer);
    free(pipe);
}

static struct yetty_ycore_size_result webasm_pipe_write(struct yetty_ycore_xthread_event_pipe *self,
                                                        const void *data, size_t size)
{
    struct yetty_yplatform_webasm_platform_input_pipe *pipe;

    pipe = container_of(self, struct yetty_yplatform_webasm_platform_input_pipe, base);

    if (size == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    /* Grow buffer if needed */
    if (pipe->buffer_size + size > pipe->buffer_capacity) {
        size_t new_cap = pipe->buffer_capacity * 2;
        char *new_buf;

        while (new_cap < pipe->buffer_size + size) {
            new_cap *= 2;
        }

        new_buf = realloc(pipe->buffer, new_cap);
        if (!new_buf) {
            return YETTY_ERR(yetty_ycore_size, "failed to grow pipe buffer");
        }

        pipe->buffer = new_buf;
        pipe->buffer_capacity = new_cap;
    }

    memcpy(pipe->buffer + pipe->buffer_size, data, size);
    pipe->buffer_size += size;

    /* Just mark as pending - main loop will process events.
	 * Avoid emscripten_async_call to prevent Asyncify issues. */
    pipe->callback_pending = 1;

    return YETTY_OK(yetty_ycore_size, size);
}

static struct yetty_ycore_size_result webasm_pipe_read(struct yetty_ycore_xthread_event_pipe *self,
                                                       void *data, size_t max_size)
{
    struct yetty_yplatform_webasm_platform_input_pipe *pipe;
    size_t to_read;

    pipe = container_of(self, struct yetty_yplatform_webasm_platform_input_pipe, base);

    if (pipe->buffer_size == 0 || max_size == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }

    to_read = (max_size < pipe->buffer_size) ? max_size : pipe->buffer_size;
    memcpy(data, pipe->buffer, to_read);
    memmove(pipe->buffer, pipe->buffer + to_read, pipe->buffer_size - to_read);
    pipe->buffer_size -= to_read;

    return YETTY_OK(yetty_ycore_size, to_read);
}

static struct yetty_ycore_int_result webasm_pipe_read_fd(
    const struct yetty_ycore_xthread_event_pipe *self)
{
    (void)self;
    return YETTY_OK(yetty_ycore_int, -1); /* No fd on webasm */
}

static struct yetty_ycore_void_result webasm_pipe_set_event_loop(
    struct yetty_ycore_xthread_event_pipe *self, struct yetty_yplatform_event_loop *loop)
{
    struct yetty_yplatform_webasm_platform_input_pipe *pipe;

    pipe = container_of(self, struct yetty_yplatform_webasm_platform_input_pipe, base);
    pipe->event_loop = loop;

    /* If there's pending data, just set the flag - main loop will process */
    if (pipe->event_loop && pipe->buffer_size > 0) {
        pipe->callback_pending = 1;
    }

    return YETTY_OK_VOID();
}

/* Create function */

struct yetty_yplatform_input_pipe_result yetty_platform_input_pipe_create(void)
{
    struct yetty_yplatform_webasm_platform_input_pipe *pipe;

    pipe = calloc(1, sizeof(struct yetty_yplatform_webasm_platform_input_pipe));
    if (!pipe) {
        return YETTY_ERR(yetty_yplatform_input_pipe, "failed to allocate webasm input pipe");
    }

    pipe->base.ops = &webasm_pipe_ops;
    pipe->buffer_capacity = PIPE_BUFFER_CAPACITY;
    pipe->buffer = malloc(pipe->buffer_capacity);
    if (!pipe->buffer) {
        free(pipe);
        return YETTY_ERR(yetty_yplatform_input_pipe, "failed to allocate pipe buffer");
    }

    return YETTY_OK(yetty_yplatform_input_pipe, &pipe->base);
}
