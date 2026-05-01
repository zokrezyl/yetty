/* WebAssembly event loop using emscripten main loop */

#include <yetty/ycore/event-loop.h>
#include <yetty/ycore/types.h>
#include <yetty/platform/platform-input-pipe.h>
#include <yetty/platform/pty-pipe-source.h>
#include <yetty/ytrace/ytrace.h>
#include <emscripten/emscripten.h>
#include <emscripten/threading.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Forward declarations for webasm pipe functions (from pipe.c) */
int yetty_yplatform_input_pipe_webasm_platform_input_pipe_has_pending(struct yetty_ycore_xthread_event_pipe *self);
void yetty_yplatform_input_pipe_webasm_platform_input_pipe_process(struct yetty_ycore_xthread_event_pipe *self);

#define MAX_LISTENERS_PER_TYPE 64
#define MAX_PTY_PIPES 16
#define MAX_TIMERS 64

struct yetty_yplatform_webasm_event_loop;

/* PTY pipe handle - drains a real fd produced by tinyemu-pty */
struct yetty_yplatform_pty_pipe_handle {
    int active;
    int fd;
    yetty_pipe_alloc_cb alloc_cb;
    yetty_pipe_read_cb read_cb;
    void *cb_ctx;
    struct yetty_yplatform_webasm_event_loop *impl;
};

struct yetty_yplatform_timer_handle {
    int id;
    int timeout_ms;
    int active;
    double last_fire;
    struct yetty_ycore_event_listener *listener;
    struct yetty_yplatform_webasm_event_loop *impl;
};

struct yetty_yplatform_prioritized_listener {
    struct yetty_ycore_event_listener *listener;
    int priority;
};

struct yetty_yplatform_webasm_event_loop {
    struct yetty_yplatform_event_loop base;

    struct yetty_yplatform_prioritized_listener listeners[YETTY_YCORE_COUNT][MAX_LISTENERS_PER_TYPE];
    int listener_counts[YETTY_YCORE_COUNT];

    struct yetty_yplatform_pty_pipe_handle pty_pipes[MAX_PTY_PIPES];

    struct yetty_yplatform_timer_handle timers[MAX_TIMERS];
    int next_timer_id;

    struct yetty_ycore_xthread_event_pipe *platform_input_pipe;
    int running;
};

/* Forward declarations */
static struct yetty_ycore_void_result webasm_destroy(struct yetty_yplatform_event_loop *self);
static struct yetty_ycore_void_result webasm_start(struct yetty_yplatform_event_loop *self);
static struct yetty_ycore_void_result webasm_stop(struct yetty_yplatform_event_loop *self);
static struct yetty_ycore_void_result webasm_register_listener(
    struct yetty_yplatform_event_loop *self, enum yetty_ycore_event_type type,
    struct yetty_ycore_event_listener *listener, int priority);
static struct yetty_ycore_void_result webasm_deregister_listener(
    struct yetty_yplatform_event_loop *self, enum yetty_ycore_event_type type,
    struct yetty_ycore_event_listener *listener);
static struct yetty_ycore_int_result webasm_dispatch(struct yetty_yplatform_event_loop *self,
                                                     const struct yetty_yui_event *event);
static struct yetty_ycore_void_result webasm_broadcast(struct yetty_yplatform_event_loop *self,
                                                       const struct yetty_yui_event *event);
static struct yetty_ycore_pipe_id_result webasm_register_pty_pipe(
    struct yetty_yplatform_event_loop *self, struct yetty_platform_pty_pipe_source *source,
    yetty_pipe_alloc_cb alloc_cb, yetty_pipe_read_cb read_cb, void *cb_ctx);
static struct yetty_ycore_void_result webasm_unregister_pty_pipe(
    struct yetty_yplatform_event_loop *self, yetty_ycore_pipe_id id);
static struct yetty_ycore_timer_id_result webasm_create_timer(struct yetty_yplatform_event_loop *self);
static struct yetty_ycore_void_result webasm_config_timer(struct yetty_yplatform_event_loop *self,
                                                          yetty_ycore_timer_id id, int timeout_ms);
static struct yetty_ycore_void_result webasm_start_timer(struct yetty_yplatform_event_loop *self,
                                                         yetty_ycore_timer_id id);
static struct yetty_ycore_void_result webasm_stop_timer(struct yetty_yplatform_event_loop *self,
                                                        yetty_ycore_timer_id id);
static struct yetty_ycore_void_result webasm_destroy_timer(struct yetty_yplatform_event_loop *self,
                                                           yetty_ycore_timer_id id);
static struct yetty_ycore_void_result webasm_register_timer_listener(
    struct yetty_yplatform_event_loop *self, yetty_ycore_timer_id id,
    struct yetty_ycore_event_listener *listener);
YETTY_EXTERNAL_CALLBACK
static void webasm_request_render(struct yetty_yplatform_event_loop *self);
static void process_pty_data(struct yetty_yplatform_pty_pipe_handle *ph);

static const struct yetty_ycore_event_loop_ops webasm_ops = {
    .destroy = webasm_destroy,
    .start = webasm_start,
    .stop = webasm_stop,
    .register_listener = webasm_register_listener,
    .deregister_listener = webasm_deregister_listener,
    .dispatch = webasm_dispatch,
    .broadcast = webasm_broadcast,
    .register_pty_pipe = webasm_register_pty_pipe,
    .unregister_pty_pipe = webasm_unregister_pty_pipe,
    .create_timer = webasm_create_timer,
    .config_timer = webasm_config_timer,
    .start_timer = webasm_start_timer,
    .stop_timer = webasm_stop_timer,
    .destroy_timer = webasm_destroy_timer,
    .register_timer_listener = webasm_register_timer_listener,
    .request_render = webasm_request_render,
};

/* Main loop tick - processes input events, PTY data, and fires timers */
static void main_loop_tick(void *arg)
{
    struct yetty_yplatform_webasm_event_loop *impl = arg;
    double now;
    int i;

    if (!impl->running) {
        return;
    }

    /* Process pending input pipe events first (keyboard, mouse, resize) */
    if (impl->platform_input_pipe &&
        yetty_yplatform_input_pipe_webasm_platform_input_pipe_has_pending(impl->platform_input_pipe)) {
        yetty_yplatform_input_pipe_webasm_platform_input_pipe_process(impl->platform_input_pipe);
    }

    /* Drain any pending PTY data (non-blocking read of pty fds). */
    for (i = 0; i < MAX_PTY_PIPES; i++) {
        if (impl->pty_pipes[i].active) {
            process_pty_data(&impl->pty_pipes[i]);
        }
    }

    now = emscripten_get_now();

    /* Fire timers that are due */
    for (i = 0; i < impl->next_timer_id; i++) {
        struct yetty_yplatform_timer_handle *th = &impl->timers[i];
        double elapsed;

        if (!th->active) {
            continue;
        }

        elapsed = now - th->last_fire;
        if (elapsed >= th->timeout_ms) {
            struct yetty_yui_event event = {0};

            th->last_fire = now;
            event.type = YETTY_YCORE_TIMER;
            event.timer.timer_id = th->id;

            if (th->listener) {
                th->listener->handler(th->listener, &event);
            }
        }
    }
}

/* Lifecycle */

static struct yetty_ycore_void_result webasm_destroy(struct yetty_yplatform_event_loop *self)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);

    struct yetty_ycore_void_result stop_r = webasm_stop(self);
    free(impl);

    if (YETTY_IS_ERR(stop_r)) {
        return YETTY_ERR(yetty_ycore_void, "webasm_destroy: stop failed", stop_r);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result webasm_start(struct yetty_yplatform_event_loop *self)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);

    if (impl->running) {
        return YETTY_OK_VOID();
    }

    impl->running = 1;

    /* Render runs on the main thread via rAF. Heavy compute (TinyEMU CPU)
     * goes to a worker created via pthread_create from a platform-pty
     * backend; pthreads on emcc map to Web Workers and share linear memory
     * via SharedArrayBuffer (requires COOP/COEP from serve.py). */
    emscripten_set_main_loop_arg(main_loop_tick, impl, 0, 0);

    ydebug("webasm_event_loop: started");

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result webasm_stop(struct yetty_yplatform_event_loop *self)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);

    if (!impl->running) {
        return YETTY_OK_VOID();
    }

    impl->running = 0;
    emscripten_cancel_main_loop();

    ydebug("webasm_event_loop: stopped");

    return YETTY_OK_VOID();
}

/* Listeners */

static struct yetty_ycore_void_result webasm_register_listener(
    struct yetty_yplatform_event_loop *self, enum yetty_ycore_event_type type,
    struct yetty_ycore_event_listener *listener, int priority)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);
    int count, i;

    if (type < 0 || type >= YETTY_YCORE_COUNT || !listener) {
        return YETTY_ERR(yetty_ycore_void, "invalid event type or listener");
    }

    count = impl->listener_counts[type];
    if (count >= MAX_LISTENERS_PER_TYPE) {
        return YETTY_ERR(yetty_ycore_void, "too many listeners");
    }

    /* Insert sorted by priority (descending) */
    for (i = count; i > 0 && impl->listeners[type][i - 1].priority < priority; i--) {
        impl->listeners[type][i] = impl->listeners[type][i - 1];
    }

    impl->listeners[type][i].listener = listener;
    impl->listeners[type][i].priority = priority;
    impl->listener_counts[type]++;

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result webasm_deregister_listener(
    struct yetty_yplatform_event_loop *self, enum yetty_ycore_event_type type,
    struct yetty_ycore_event_listener *listener)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);
    int count, i;

    if (type < 0 || type >= YETTY_YCORE_COUNT || !listener) {
        return YETTY_ERR(yetty_ycore_void, "invalid event type or listener");
    }

    count = impl->listener_counts[type];
    for (i = 0; i < count; i++) {
        if (impl->listeners[type][i].listener == listener) {
            /* Shift remaining listeners down */
            for (; i < count - 1; i++) {
                impl->listeners[type][i] = impl->listeners[type][i + 1];
            }
            impl->listener_counts[type]--;
            return YETTY_OK_VOID();
        }
    }

    return YETTY_ERR(yetty_ycore_void, "listener not found");
}

static struct yetty_ycore_int_result webasm_dispatch(struct yetty_yplatform_event_loop *self,
                                                     const struct yetty_yui_event *event)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);
    int type = event->type;
    int count, i;

    if (type < 0 || type >= YETTY_YCORE_COUNT) {
        return YETTY_ERR(yetty_ycore_int, "invalid event type");
    }

    count = impl->listener_counts[type];
    for (i = 0; i < count; i++) {
        struct yetty_ycore_event_listener *listener = impl->listeners[type][i].listener;
        struct yetty_ycore_int_result res = listener->handler(listener, event);

        if (YETTY_IS_ERR(res)) {
            return res;
        }
        if (res.value) {
            return YETTY_OK(yetty_ycore_int, 1);
        }
    }

    return YETTY_OK(yetty_ycore_int, 0);
}

static struct yetty_ycore_void_result webasm_broadcast(struct yetty_yplatform_event_loop *self,
                                                       const struct yetty_yui_event *event)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);
    int t, count, i;

    for (t = 0; t < YETTY_YCORE_COUNT; t++) {
        count = impl->listener_counts[t];
        for (i = 0; i < count; i++) {
            struct yetty_ycore_event_listener *listener = impl->listeners[t][i].listener;
            struct yetty_ycore_int_result res = listener->handler(listener, event);

            if (!YETTY_IS_OK(res)) {
                return YETTY_ERR(yetty_ycore_void, res.error.msg);
            }
        }
    }

    return YETTY_OK_VOID();
}

/* PTY Pipe - drains the pty fd produced by tinyemu-pty (real pipe(2) fd).
 * The VM thread writes to pty_pipe[1] from inside its libuv loop; we read
 * pty_pipe[0] non-blockingly from the emscripten main-loop tick. */

static void process_pty_data(struct yetty_yplatform_pty_pipe_handle *ph)
{
    char *buf = NULL;
    size_t buflen = 0;
    ssize_t n;

    if (!ph->active || ph->fd < 0 || !ph->alloc_cb || !ph->read_cb) {
        return;
    }

    ph->alloc_cb(ph->cb_ctx, 4096, &buf, &buflen);
    if (!buf || buflen == 0) {
        return;
    }

    n = read(ph->fd, buf, buflen);
    if (n <= 0) {
        /* EAGAIN/EWOULDBLOCK or eof - nothing to do this tick */
        return;
    }

    ph->read_cb(ph->cb_ctx, buf, (long)n);
}

static struct yetty_ycore_pipe_id_result webasm_register_pty_pipe(
    struct yetty_yplatform_event_loop *self, struct yetty_platform_pty_pipe_source *source,
    yetty_pipe_alloc_cb alloc_cb, yetty_pipe_read_cb read_cb, void *cb_ctx)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);
    struct yetty_yplatform_pty_pipe_handle *ph;
    int id, fd, flags;

    if (!source || !alloc_cb || !read_cb) {
        return YETTY_ERR(yetty_ycore_pipe_id, "invalid source or callbacks");
    }

    /* Find free slot */
    for (id = 0; id < MAX_PTY_PIPES; id++) {
        if (!impl->pty_pipes[id].active) {
            break;
        }
    }
    if (id >= MAX_PTY_PIPES) {
        return YETTY_ERR(yetty_ycore_pipe_id, "too many pty pipes");
    }

    fd = (int)source->abstract;
    /* Force non-blocking so process_pty_data never stalls the rAF tick. */
    flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    ph = &impl->pty_pipes[id];
    memset(ph, 0, sizeof(*ph));
    ph->active = 1;
    ph->fd = fd;
    ph->alloc_cb = alloc_cb;
    ph->read_cb = read_cb;
    ph->cb_ctx = cb_ctx;
    ph->impl = impl;

    ydebug("webasm_event_loop: register_pty_pipe id=%d fd=%d", id, fd);

    return YETTY_OK(yetty_ycore_pipe_id, id);
}

static struct yetty_ycore_void_result webasm_unregister_pty_pipe(
    struct yetty_yplatform_event_loop *self, yetty_ycore_pipe_id id)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);

    if (id < 0 || id >= MAX_PTY_PIPES) {
        return YETTY_ERR(yetty_ycore_void, "invalid pipe id");
    }

    if (impl->pty_pipes[id].active) {
        impl->pty_pipes[id].active = 0;
        impl->pty_pipes[id].fd = -1;
    }

    ydebug("webasm_event_loop: unregister_pty_pipe id=%d", id);

    return YETTY_OK_VOID();
}

/* Timer */

static struct yetty_ycore_timer_id_result webasm_create_timer(struct yetty_yplatform_event_loop *self)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);
    int id = impl->next_timer_id++;
    struct yetty_yplatform_timer_handle *th;

    if (id >= MAX_TIMERS) {
        return YETTY_ERR(yetty_ycore_timer_id, "too many timers");
    }

    th = &impl->timers[id];
    memset(th, 0, sizeof(*th));
    th->id = id;
    th->impl = impl;

    ydebug("webasm_event_loop: create_timer id=%d", id);

    return YETTY_OK(yetty_ycore_timer_id, id);
}

static struct yetty_ycore_void_result webasm_config_timer(struct yetty_yplatform_event_loop *self,
                                                          yetty_ycore_timer_id id, int timeout_ms)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);
    struct yetty_yplatform_timer_handle *th;

    if (id < 0 || id >= MAX_TIMERS) {
        return YETTY_ERR(yetty_ycore_void, "invalid timer id");
    }

    th = &impl->timers[id];
    th->timeout_ms = timeout_ms;

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result webasm_start_timer(struct yetty_yplatform_event_loop *self,
                                                         yetty_ycore_timer_id id)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);
    struct yetty_yplatform_timer_handle *th;

    if (id < 0 || id >= MAX_TIMERS) {
        return YETTY_ERR(yetty_ycore_void, "invalid timer id");
    }

    th = &impl->timers[id];
    th->active = 1;
    th->last_fire = emscripten_get_now();

    ydebug("webasm_event_loop: start_timer id=%d timeout=%d", id, th->timeout_ms);

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result webasm_stop_timer(struct yetty_yplatform_event_loop *self,
                                                        yetty_ycore_timer_id id)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);

    if (id < 0 || id >= MAX_TIMERS) {
        return YETTY_ERR(yetty_ycore_void, "invalid timer id");
    }

    impl->timers[id].active = 0;

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result webasm_destroy_timer(struct yetty_yplatform_event_loop *self,
                                                           yetty_ycore_timer_id id)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);

    if (id < 0 || id >= MAX_TIMERS) {
        return YETTY_ERR(yetty_ycore_void, "invalid timer id");
    }

    impl->timers[id].active = 0;
    impl->timers[id].listener = NULL;

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result webasm_register_timer_listener(
    struct yetty_yplatform_event_loop *self, yetty_ycore_timer_id id,
    struct yetty_ycore_event_listener *listener)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);

    if (id < 0 || id >= MAX_TIMERS || !listener) {
        return YETTY_ERR(yetty_ycore_void, "invalid timer id or listener");
    }

    impl->timers[id].listener = listener;

    return YETTY_OK_VOID();
}

/* Render request */

YETTY_EXTERNAL_CALLBACK
static void webasm_request_render(struct yetty_yplatform_event_loop *self)
{
    struct yetty_yplatform_webasm_event_loop *impl = container_of(self, struct yetty_yplatform_webasm_event_loop, base);
    struct yetty_yui_event event = {0};

    event.type = YETTY_YCORE_RENDER;
    webasm_dispatch(self, &event);

    (void)impl;
}

/* Factory */

struct yetty_ycore_event_loop_result yetty_ycore_event_loop_create(
    struct yetty_ycore_xthread_event_pipe *pipe)
{
    struct yetty_yplatform_webasm_event_loop *impl;

    impl = calloc(1, sizeof(struct yetty_yplatform_webasm_event_loop));
    if (!impl) {
        return YETTY_ERR(yetty_ycore_event_loop, "failed to allocate webasm event loop");
    }

    impl->base.ops = &webasm_ops;
    impl->platform_input_pipe = pipe;

    /* On webasm, the platform input pipe uses async callbacks (no fd).
	 * Set the event loop reference so the pipe can dispatch events. */
    if (pipe && pipe->ops && pipe->ops->set_event_loop) {
        pipe->ops->set_event_loop(pipe, &impl->base);
    }

    ydebug("webasm_event_loop: created at %p", (void *)impl);

    return YETTY_OK(yetty_ycore_event_loop, &impl->base);
}
