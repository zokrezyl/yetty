/*
 * ygui_engine_uv.c — libuv-driven event-loop integration for ygui.
 *
 * Lives in the `ygui` static library (layered on top of `ygui-core`).
 * ygui-core itself never references libuv: the engine struct carries
 * an opaque `void *uv_state` + a destroy hook (see ygui_internal.h),
 * which this file populates when a caller invokes
 * yetty_ygui_old_engine_attach / _run / _poll / _get_loop.
 *
 * Two responsibilities:
 *
 *  1. Drive the engine — stdin poll (input), prepare handle (auto-render),
 *     SIGWINCH pickup.
 *  2. Install a yetty_platform_pty implementation that wraps the
 *     engine's output_fd via uv_pipe_t + uv_write. ygui_osc.c writes
 *     OSC bytes exclusively through engine->output_pty->ops->write —
 *     pure memcpy + linked-list append from the producer's view; libuv
 *     drains the kernel buffer in the background. The producer never
 *     blocks on EAGAIN and large envelopes (multi-MB yimage payloads,
 *     hypothetically 200GB video) are never truncated.
 *
 * Standalone tools that drive their own UI (ygreeter, ytop, …) link
 * `ygui` (full) and call yetty_ygui_old_engine_run. Embedders / cross
 * targets where libuv isn't a fit (webasm) link `ygui-core` alone —
 * the engine constructs and destroys cleanly without ever entering
 * the libuv code path.
 */

#include "ygui_internal.h"
#include <yetty/yface/yface.h>
#include <yetty/yplatform/pty.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <uv.h>

#ifdef _WIN32
#include <io.h>
#define STDIN_FILENO 0
#else
#include <unistd.h>
#include <sys/ioctl.h>
#endif

/*=============================================================================
 * uv-backed pty implementation
 *
 * Implements struct yetty_platform_pty so ygui_osc.c can target
 * engine->output_pty exactly like it targets the in-process memory-pty
 * yui uses. The read/resize/stop slots are stubs — ygui doesn't read
 * from its output pty (input comes via engine->input_fd / stdin_poll).
 *
 * Write path (queue-driven, single uv_write in flight):
 *   - The producer (ygui_osc.c) calls ops->write. We malloc+memcpy the
 *     bytes into a fresh write request and push it on the tail of the
 *     queue.
 *   - If no uv_write is currently outstanding, kick the head request.
 *     uv_write is non-blocking — libuv buffers and reports completion
 *     via the callback. The producer returns immediately.
 *   - on_write_done frees the request and kicks the next.
 *
 * Late errors (failed uv_write submission, callback status != 0) are
 * logged via yerror. The producer has already returned by then; there
 * is no synchronous error channel. The underlying pipe tearing down
 * also surfaces as EOF on the read side, which shuts the engine.
 *===========================================================================*/

struct ygui_uv_write_req {
    uv_write_t req;
    char *data;
    size_t len;
    struct ygui_uv_write_req *next;
    struct ygui_uv_pty *owner;
};

struct ygui_uv_pty {
    struct yetty_platform_pty base;
    uv_pipe_t pipe;
    int pipe_open;
    struct ygui_uv_write_req *head;
    struct ygui_uv_write_req *tail;
    int in_flight;
};

static void ygui_uv_pty_kick(struct ygui_uv_pty *p);

YETTY_EXTERNAL_CALLBACK
static void ygui_uv_pty_on_write_done(uv_write_t *req, int status)
{
    struct ygui_uv_write_req *wr = (struct ygui_uv_write_req *)req;
    struct ygui_uv_pty *p = wr->owner;
    if (status != 0) {
        yerror("ygui_uv_pty: uv_write completed with status=%d (%s); %zu bytes lost", status,
               uv_strerror(status), wr->len);
    }
    free(wr->data);
    free(wr);
    if (p) {
        p->in_flight = 0;
        ygui_uv_pty_kick(p);
    }
}

static void ygui_uv_pty_kick(struct ygui_uv_pty *p)
{
    if (!p || p->in_flight || !p->head || !p->pipe_open) {
        return;
    }
    struct ygui_uv_write_req *wr = p->head;
    p->head = wr->next;
    if (!p->head) {
        p->tail = NULL;
    }
    wr->next = NULL;
    p->in_flight = 1;
    uv_buf_t buf = uv_buf_init(wr->data, (unsigned)wr->len);
    int rc = uv_write(&wr->req, (uv_stream_t *)&p->pipe, &buf, 1, ygui_uv_pty_on_write_done);
    if (rc != 0) {
        yerror("ygui_uv_pty: uv_write submit failed: %s; %zu bytes dropped", uv_strerror(rc),
               wr->len);
        free(wr->data);
        free(wr);
        p->in_flight = 0;
        /* Drain whatever else is queued — caller's data has already been
         * accepted (memcpy'd into queue), no way to signal back. */
        ygui_uv_pty_kick(p);
    }
}

static struct yetty_ycore_size_result ygui_uv_pty_op_write(struct yetty_platform_pty *self,
                                                           const char *data, size_t len)
{
    struct ygui_uv_pty *p = (struct ygui_uv_pty *)self;
    if (!p) {
        return YETTY_ERR(yetty_ycore_size, "ygui_uv_pty: self is NULL");
    }
    if (!p->pipe_open) {
        return YETTY_ERR(yetty_ycore_size, "ygui_uv_pty: pipe not open");
    }
    if (len == 0) {
        return YETTY_OK(yetty_ycore_size, 0);
    }
    struct ygui_uv_write_req *wr = (struct ygui_uv_write_req *)calloc(1, sizeof(*wr));
    if (!wr) {
        return YETTY_ERR(yetty_ycore_size, "ygui_uv_pty: write req alloc failed");
    }
    wr->data = (char *)malloc(len);
    if (!wr->data) {
        free(wr);
        return YETTY_ERR(yetty_ycore_size, "ygui_uv_pty: write data alloc failed");
    }
    memcpy(wr->data, data, len);
    wr->len = len;
    wr->next = NULL;
    wr->owner = p;
    if (p->tail) {
        p->tail->next = wr;
    } else {
        p->head = wr;
    }
    p->tail = wr;
    ygui_uv_pty_kick(p);
    return YETTY_OK(yetty_ycore_size, len);
}

static struct yetty_ycore_size_result ygui_uv_pty_op_read(struct yetty_platform_pty *self,
                                                          char *buf, size_t max_len)
{
    (void)self;
    (void)buf;
    (void)max_len;
    /* ygui's output pty is write-only — input comes from engine->input_fd
     * via the stdin_poll handler. */
    return YETTY_ERR(yetty_ycore_size, "ygui_uv_pty: read not supported");
}

static struct yetty_ycore_void_result ygui_uv_pty_op_resize(struct yetty_platform_pty *self,
                                                            uint32_t cols, uint32_t rows,
                                                            uint32_t pixel_w, uint32_t pixel_h)
{
    (void)self;
    (void)cols;
    (void)rows;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ygui_uv_pty_op_stop(struct yetty_platform_pty *self)
{
    (void)self;
    return YETTY_OK_VOID();
}

static struct yetty_platform_pty_pipe_source *ygui_uv_pty_op_pipe_source(
    struct yetty_platform_pty *self)
{
    (void)self;
    return NULL;
}

YETTY_EXTERNAL_CALLBACK
static void ygui_uv_pty_close_cb(uv_handle_t *handle)
{
    /* The handle is embedded in struct ygui_uv_pty; the wrapper itself
     * is freed in op_destroy after this callback completes. */
    (void)handle;
}

static struct yetty_ycore_void_result ygui_uv_pty_op_destroy(struct yetty_platform_pty *self)
{
    struct ygui_uv_pty *p = (struct ygui_uv_pty *)self;
    if (!p) {
        return YETTY_OK_VOID();
    }
    /* engine_destroy queues the exit-time OSCs (unsubscribe clicks/moves,
     * kill_card, CARD_REMOVE) through ops->write right before tearing
     * the pty down. Those writes get appended to p->head; only the
     * already-in-flight request would actually reach the fd on its own.
     * If we free the queue tail without flushing, the receiving terminal
     * never sees the cleanup OSCs — the card stays painted, mouse
     * subscriptions stay on, and the ymgui-layer keeps routing events
     * to a process that's gone. Drain by pumping the loop until both
     * the queue and the in-flight slot are empty. The safety counter
     * caps the worst case (pipe permanently EAGAIN, peer dead, etc.). */
    if (p->pipe_open && (p->head || p->in_flight)) {
        uv_loop_t *loop = ((uv_handle_t *)&p->pipe)->loop;
        if (loop) {
            int safety = 4096;
            while ((p->head || p->in_flight) && safety-- > 0) {
                uv_run(loop, UV_RUN_NOWAIT);
            }
            if (p->head || p->in_flight) {
                yerror("ygui_uv_pty: flush gave up with %s%s pending",
                       p->in_flight ? "in-flight " : "", p->head ? "queued" : "");
            }
        }
    }
    /* Anything still queued after the flush is unreachable — peer closed
     * or loop refused to make progress. Free without leaking. */
    while (p->head) {
        struct ygui_uv_write_req *wr = p->head;
        p->head = wr->next;
        free(wr->data);
        free(wr);
    }
    p->tail = NULL;
    if (p->pipe_open) {
        p->pipe_open = 0;
        uv_close((uv_handle_t *)&p->pipe, ygui_uv_pty_close_cb);
    }
    free(p);
    return YETTY_OK_VOID();
}

static const struct yetty_platform_pty_ops ygui_uv_pty_ops = {
    .destroy = ygui_uv_pty_op_destroy,
    .read = ygui_uv_pty_op_read,
    .write = ygui_uv_pty_op_write,
    .resize = ygui_uv_pty_op_resize,
    .stop = ygui_uv_pty_op_stop,
    .pipe_source = ygui_uv_pty_op_pipe_source,
};

/* Wrap an open file descriptor (typically stdout) in a yetty_platform_pty
 * whose ops->write is queued via uv_write. Returns NULL if the pipe
 * can't be opened. */
static struct yetty_platform_pty *ygui_uv_pty_wrap_fd(uv_loop_t *loop, int fd)
{
    struct ygui_uv_pty *p = (struct ygui_uv_pty *)calloc(1, sizeof(*p));
    if (!p) {
        return NULL;
    }
    p->base.ops = &ygui_uv_pty_ops;
    if (uv_pipe_init(loop, &p->pipe, 0) != 0) {
        free(p);
        return NULL;
    }
    if (uv_pipe_open(&p->pipe, fd) != 0) {
        uv_close((uv_handle_t *)&p->pipe, NULL);
        free(p);
        return NULL;
    }
    p->pipe_open = 1;
    return &p->base;
}

/*=============================================================================
 * Engine libuv state
 *===========================================================================*/

struct ygui_uv_state {
    uv_loop_t *loop;
    int owns_loop;
    uv_poll_t stdin_poll;
    uv_prepare_t prepare_handle;
    /* SIGWINCH wakeup. libuv installs its own sigprocmask which
     * silently masks the raw signal() handler that engine.c installs;
     * using uv_signal_t is the contract-correct way to react to a
     * signal from inside a libuv loop. Fires sigwinch_cb which simply
     * sets the resize-pending flag — prepare_cb does the actual ioctl
     * re-read. */
    uv_signal_t sigwinch_handle;
};

static struct ygui_uv_state *uv_state_of(struct yetty_ygui_old_engine *engine)
{
    return engine ? (struct ygui_uv_state *)engine->uv_state : NULL;
}

static void uv_state_destroy(struct yetty_ygui_old_engine *engine)
{
    struct ygui_uv_state *s = uv_state_of(engine);
    if (!s) {
        return;
    }
    /* Tear down the uv-backed output pty, if we created it. */
    if (engine->output_pty_owned && engine->output_pty) {
        struct yetty_ycore_void_result dr = engine->output_pty->ops->destroy(engine->output_pty);
        if (YETTY_IS_ERR(dr)) {
            yerror("ygui_uv: output_pty destroy: %s", dr.error.msg);
            yetty_ycore_error_destroy(dr.error);
        }
        engine->output_pty = NULL;
        engine->output_pty_owned = 0;
    }
    if (s->owns_loop && s->loop) {
        uv_loop_close(s->loop);
        free(s->loop);
    }
    free(s);
    engine->uv_state = NULL;
    engine->uv_state_destroy_cb = NULL;
}

/*-----------------------------------------------------------------------------
 * Callbacks
 *---------------------------------------------------------------------------*/

YETTY_EXTERNAL_CALLBACK
static void stdin_poll_cb(uv_poll_t *handle, int status, int events)
{
    struct yetty_ygui_old_engine *engine = (struct yetty_ygui_old_engine *)handle->data;
    if (status < 0) {
        return;
    }

    if (events & UV_READABLE) {
        char buf[1024];
        ssize_t n = read(engine->input_fd, buf, sizeof(buf));
        if (n > 0) {
            if (engine->yface_in) {
                /* yface scans for \e]…\e\\ envelopes, calls on_osc per
                 * complete envelope, and on_raw for the bytes in between. */
                yetty_yface_feed_bytes(engine->yface_in, buf, (size_t)n);
            } else {
                /* No yface available — fall back to legacy text parser. */
                yetty_ygui_old_internal_process_input(engine, buf, (int)n);
            }
        } else if (n == 0) {
            /* EOF */
            engine->running = 0;
        }
    }
}

YETTY_EXTERNAL_CALLBACK
static void sigwinch_cb(uv_signal_t *handle, int signum)
{
    (void)handle;
    (void)signum;
    /* Same flag the raw signal() handler in engine.c flips. prepare_cb
     * is the consumer; we don't do any wire work here. */
    yetty_ygui_old_internal_resize_pending = 1;
}

YETTY_EXTERNAL_CALLBACK
static void prepare_cb(uv_prepare_t *handle)
{
    struct yetty_ygui_old_engine *engine = (struct yetty_ygui_old_engine *)handle->data;
    struct ygui_uv_state *s = uv_state_of(engine);

    /* Check for terminal resize. We re-read TIOCGWINSZ every prepare
     * tick (one cheap ioctl), not only on the SIGWINCH flag, because
     * the signal is fundamentally racy: yetty fires the post-layout
     * resize as soon as the YUI settles, which can land before the
     * child has installed its SIGWINCH handler (default disposition is
     * "ignore"), AND because libuv's own signal plumbing can swallow
     * delivery. The ioctl returns instantly when nothing changed, so
     * the poll is essentially free. */
    {
        yetty_ygui_old_internal_resize_pending = 0;
#ifndef _WIN32
        /* Pick up the new host-terminal cell count AND pixel size.
         * yetty populates ws_xpixel/ws_ypixel in TIOCGWINSZ alongside
         * ws_col/ws_row; the engine used to wait for an SC_RESIZE OSC
         * from the host to learn the real pixel size, but the
         * figure-tree path no longer emits one — read the pixel area
         * straight from the ioctl instead. */
        struct winsize ws;
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
            int new_w = ws.ws_col;
            int new_h = ws.ws_row;
            if (new_w != engine->card_w || new_h != engine->card_h) {
                engine->card_w = new_w;
                engine->card_h = new_h;
                struct yetty_ycore_void_result pr =
                    yetty_ygui_old_osc_card_place(engine->output_pty, engine->figure_id, engine->card_x,
                                              engine->card_y, (uint32_t)new_w, (uint32_t)new_h);
                if (YETTY_IS_ERR(pr)) {
                    yerror("ygui_uv: card_place on SIGWINCH: %s", pr.error.msg);
                    yetty_ycore_error_destroy(pr.error);
                }
            }
            if (ws.ws_xpixel > 0 && ws.ws_ypixel > 0 &&
                ((float)ws.ws_xpixel != engine->display_pixel_w ||
                 (float)ws.ws_ypixel != engine->display_pixel_h)) {
                /* Path the figure-tree wire used to drive via
                 * SC_CLIENT_INPUT_FIGURE_RESIZE: set the pixel-size
                 * fields + needs_resize, the next render runs
                 * handle_resize which updates engine->width/height
                 * AND fires the user's resize_callback (ygreeter's
                 * outer-window stretcher hangs off this). */
                engine->display_pixel_w = (float)ws.ws_xpixel;
                engine->display_pixel_h = (float)ws.ws_ypixel;
                engine->have_pixel_size = 1;
                if (engine->reference_w == 0.0f) {
                    engine->reference_w = engine->display_pixel_w;
                    engine->reference_h = engine->display_pixel_h;
                }
                engine->needs_resize = 1;
                engine->dirty = 1;
                engine->needs_full_redraw = 1;
            }
        }
#endif
    }

    /* Auto-render if dirty */
    if (engine->dirty) {
        struct yetty_ycore_void_result rr = yetty_ygui_old_engine_render(engine);
        if (YETTY_IS_ERR(rr)) {
            yerror("ygui_uv: engine_render: %s", rr.error.msg);
            yetty_ycore_error_destroy(rr.error);
        }
    }

    /* Check if we should stop */
    if (!engine->running && s && s->loop) {
        uv_stop(s->loop);
    }
}

/*-----------------------------------------------------------------------------
 * Public API
 *---------------------------------------------------------------------------*/

/* Framework-internal bootstrap. Called from engine_internal_create
 * immediately after the engine struct is allocated. Wires up:
 *   - The libuv loop (allocated + owned here unless caller supplied
 *     `borrowed_loop`, in which case it's borrowed and never closed).
 *   - The output pty (libuv pipe wrapping engine->output_fd unless
 *     caller supplied `borrowed_pty`, which is the yui memory-ring
 *     case — borrowed and not destroyed).
 *   - stdin polling, prepare-cb auto-render, yface input handlers.
 *   - The init OSC handshake (query_cell_size, subscribes, CARD_PLACE,
 *     CANVAS_FIT placeholder).
 * Errors chain. Caller (engine_internal_create) destroys the engine on
 * failure, which tears down whatever we did get wired up. */
struct yetty_ycore_void_result yetty_ygui_old_engine_internal_bootstrap_runtime(
    struct yetty_ygui_old_engine *engine, struct yetty_platform_pty *borrowed_pty,
    uv_loop_t *borrowed_loop)
{
    if (!engine) {
        return YETTY_ERR(yetty_ycore_void, "bootstrap_runtime: NULL engine");
    }

    struct ygui_uv_state *s = (struct ygui_uv_state *)calloc(1, sizeof(*s));
    if (!s) {
        return YETTY_ERR(yetty_ycore_void, "bootstrap_runtime: uv_state alloc");
    }
    engine->uv_state = s;
    engine->uv_state_destroy_cb = uv_state_destroy;

    /* Loop: caller-supplied (borrowed) or allocate one (owned). */
    if (borrowed_loop) {
        s->loop = borrowed_loop;
        s->owns_loop = 0;
    } else {
        s->loop = (uv_loop_t *)malloc(sizeof(uv_loop_t));
        if (!s->loop) {
            return YETTY_ERR(yetty_ycore_void, "bootstrap_runtime: loop alloc");
        }
        if (uv_loop_init(s->loop) != 0) {
            free(s->loop);
            s->loop = NULL;
            return YETTY_ERR(yetty_ycore_void, "bootstrap_runtime: uv_loop_init failed");
        }
        s->owns_loop = 1;
    }

    /* yface inbound dispatch. */
    if (engine->yface_in) {
        yetty_yface_set_handlers(engine->yface_in, yetty_ygui_old_internal_yface_on_osc,
                                 yetty_ygui_old_internal_yface_on_raw, engine);
    }

    /* stdin poll. */
    if (uv_poll_init(s->loop, &s->stdin_poll, engine->input_fd) != 0) {
        return YETTY_ERR(yetty_ycore_void, "bootstrap_runtime: stdin uv_poll_init failed");
    }
    s->stdin_poll.data = engine;
    if (uv_poll_start(&s->stdin_poll, UV_READABLE, stdin_poll_cb) != 0) {
        return YETTY_ERR(yetty_ycore_void, "bootstrap_runtime: stdin uv_poll_start failed");
    }

    /* prepare-cb auto-render. */
    if (uv_prepare_init(s->loop, &s->prepare_handle) != 0) {
        return YETTY_ERR(yetty_ycore_void, "bootstrap_runtime: prepare uv_prepare_init failed");
    }
    s->prepare_handle.data = engine;
    if (uv_prepare_start(&s->prepare_handle, prepare_cb) != 0) {
        return YETTY_ERR(yetty_ycore_void, "bootstrap_runtime: prepare uv_prepare_start failed");
    }

    /* SIGWINCH wakeup via uv_signal_t. The signal() handler in
     * engine.c also flips yetty_ygui_old_internal_resize_pending, but
     * libuv installs its own sigprocmask and may swallow the raw
     * signal — the uv_signal_t path is the contract-correct way. */
    if (uv_signal_init(s->loop, &s->sigwinch_handle) != 0) {
        return YETTY_ERR(yetty_ycore_void, "bootstrap_runtime: uv_signal_init failed");
    }
    s->sigwinch_handle.data = engine;
    if (uv_signal_start(&s->sigwinch_handle, sigwinch_cb, SIGWINCH) != 0) {
        return YETTY_ERR(yetty_ycore_void, "bootstrap_runtime: uv_signal_start failed");
    }

    /* Output pty: caller-supplied (borrowed — yui's memory ring) or
     * allocate a libuv pipe wrapping STDOUT (owned). Producers write
     * OSCs through output_pty->ops->write; the queued uv_write keeps
     * the render thread non-blocking. */
    if (borrowed_pty) {
        engine->output_pty = borrowed_pty;
        engine->output_pty_owned = 0;
    } else {
        struct yetty_platform_pty *pty = ygui_uv_pty_wrap_fd(s->loop, engine->output_fd);
        if (!pty) {
            return YETTY_ERR(yetty_ycore_void,
                             "bootstrap_runtime: could not wrap output_fd as uv-pipe pty");
        }
        engine->output_pty = pty;
        engine->output_pty_owned = 1;
    }

    /* The pty is live now — fire the init OSCs. Errors here are fatal
     * to construction; the caller destroys the engine and propagates. */
    struct yetty_ycore_void_result hr = yetty_ygui_old_engine_internal_emit_handshake(engine);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, hr, "bootstrap_runtime: handshake failed");

    return YETTY_OK_VOID();
}

void yetty_ygui_old_engine_run(struct yetty_ygui_old_engine *engine)
{
    if (!engine) {
        return;
    }
    struct ygui_uv_state *s = uv_state_of(engine);
    if (!s || !s->loop) {
        yerror("ygui_engine_run: engine has no loop — was bootstrap_runtime called?");
        return;
    }
    engine->running = 1;
    uv_run(s->loop, UV_RUN_DEFAULT);
    /* Stop our handles so engine_destroy's uv_loop_close won't error
     * with EBUSY. (Owned-loop case; for borrowed loops the caller is
     * responsible for stopping its own handles too.) */
    uv_poll_stop(&s->stdin_poll);
    uv_prepare_stop(&s->prepare_handle);
    uv_signal_stop(&s->sigwinch_handle);
}

/*=============================================================================
 * Public construction — lives here (not in ygui_engine.c) because the
 * bootstrap step requires libuv, which ygui_core is deliberately
 * free of. ygui_core only sees engine_internal_alloc; the libuv-side
 * stitch is here.
 *===========================================================================*/

struct ygui_engine_ptr_result yetty_ygui_old_engine_create(struct yetty_ygui_old_engine_args args)
{
    struct ygui_engine_ptr_result alloc_r = yetty_ygui_old_engine_internal_alloc(args.name, args.theme);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    struct yetty_ygui_old_engine *engine = alloc_r.value;
    struct yetty_ycore_void_result br =
        yetty_ygui_old_engine_internal_bootstrap_runtime(engine, /*borrowed_pty=*/NULL,
                                                     /*borrowed_loop=*/NULL);
    if (YETTY_IS_ERR(br)) {
        /* engine_destroy tears down whatever the partial bootstrap did
         * manage to wire up. */
        struct yetty_ycore_void_result dr = yetty_ygui_old_engine_destroy(engine);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        return YETTY_ERR(ygui_engine_ptr, "yetty_ygui_old_engine_create: runtime bootstrap failed", br);
    }
    return YETTY_OK(ygui_engine_ptr, engine);
}

uv_loop_t *yetty_ygui_old_engine_get_loop(struct yetty_ygui_old_engine *engine)
{
    struct ygui_uv_state *s = uv_state_of(engine);
    return s ? s->loop : NULL;
}

int yetty_ygui_old_engine_poll(struct yetty_ygui_old_engine *engine)
{
    struct ygui_uv_state *s = uv_state_of(engine);
    if (!s || !s->loop) {
        return 0;
    }
    return uv_run(s->loop, UV_RUN_NOWAIT);
}
