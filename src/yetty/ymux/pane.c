/*
 * pane.c — ymux_pane: the GPU-free logical-terminal owner.
 *
 * Extracted from yterminal/terminal.c: this is the model half of one terminal —
 * PTY + wire statemachine + bare yvterm:grid + emit yface + host-side ywire
 * channel connection — with no view, no GPU, no renderer figure. The legacy
 * renderer (terminal.c) wraps a pane; a headless server holds it bare. Plain C:
 * the pane holds the yclass grid object but is not itself a yclass class.
 */
#include <yetty/ymux/pane.h>

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yconfig/config.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yface/yface.h>
#include <yetty/yevent/event-loop.h>
#include <yetty/yplatform/pty.h>
#include <yetty/yplatform/pty-pipe-source.h>
#include <yetty/yplatform/time.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yvterm/grid.h>
#include <yetty/ywire/connection.h>
#include <yetty/ywire/wire-statemachine.h>

/* One reusable per-pane PTY read buffer, lazily allocated. 64KB matches libuv's
 * default suggested_size and is plenty for one PTY chunk; the read callback
 * drains it before the next call. */
#define YETTY_YMUX_PANE_PTY_READ_BUF_SIZE (64 * 1024)

struct yetty_ymux_pane {
    struct yetty_context context; /* POD, by value; holds pty_factory + event_loop + runtime */
    struct yetty_platform_pty *pty;
    struct yetty_ywire_wire_statemachine *sm;
    struct yetty_yclass_object *grid; /* bare yvterm:grid — the truth (owned) */
    struct yetty_yface *emit_yface;
    struct yetty_ywire_connection *channel_host;

    char *pty_read_buf;
    yetty_yevent_pipe_id pty_pipe_id;

    uint32_t cols;
    uint32_t rows;
    int shutting_down;

    yetty_ymux_pane_notify_fn notify_fn;
    void *notify_userdata;
    yetty_ymux_pane_child_exit_fn child_exit_fn;
    void *child_exit_userdata;
    yetty_ymux_pane_output_tap_fn output_tap_fn;
    void *output_tap_userdata;
};

/*===========================================================================
 * PTY I/O plumbing
 *=========================================================================*/

/* libuv-shaped buffer-alloc callback; signature dictated by yetty_pipe_alloc_cb. */
YETTY_EXTERNAL_CALLBACK
static void pane_pty_pipe_alloc(void *ctx, size_t suggested_size, char **buf, size_t *buflen)
{
    (void)suggested_size;
    struct yetty_ymux_pane *pane = ctx;
    if (!pane->pty_read_buf) {
        pane->pty_read_buf = malloc(YETTY_YMUX_PANE_PTY_READ_BUF_SIZE);
        if (!pane->pty_read_buf) {
            *buf = NULL;
            *buflen = 0;
            return;
        }
    }
    *buf = pane->pty_read_buf;
    *buflen = YETTY_YMUX_PANE_PTY_READ_BUF_SIZE;
}

/* libuv-shaped pipe-read callback. Errors from feed have no Result to propagate
 * to — surface fatal errors on the loop and absorb at this boundary. */
YETTY_EXTERNAL_CALLBACK
static void pane_pty_pipe_read(void *ctx, const char *buf, long nread)
{
    struct yetty_ymux_pane *pane = ctx;

    if (nread > 0) {
        ydebug("ymux_pane: pty read %ld bytes", nread);
        /* Tap the raw PTY output BEFORE the statemachine consumes it — the ymux
         * server ships these exact bytes to attached remote clients, which
         * re-run the same emulator over them. */
        if (pane->output_tap_fn) {
            pane->output_tap_fn(buf, (size_t)nread, pane->output_tap_userdata);
        }
        /* feed() resumes the SM coro itself — the scanner runs as far as it can,
         * then yields when it needs more bytes (or surfaces a fatal error). */
        struct yetty_ycore_void_result fr =
            yetty_ywire_wire_statemachine_feed(pane->sm, buf, (size_t)nread);
        if (YETTY_IS_ERR(fr)) {
            struct yetty_ycore_void_result wrap =
                YETTY_ERR(yetty_ycore_void, "ymux_pane: wire_statemachine_feed failed", fr);
            struct yetty_yevent_event_loop *loop = pane->context.event_loop;
            loop->ops->post_fatal_error(loop, wrap.error);
            return;
        }
        /* Hand off to the owner (renderer requests a frame; server marks the
         * pane). It reads whatever dirty state the feed produced. */
        if (pane->notify_fn) {
            struct yetty_ycore_void_result nr = pane->notify_fn(pane->notify_userdata);
            if (YETTY_IS_ERR(nr)) {
                yetty_ycore_error_destroy(nr.error);
            }
        }
    } else if (nread < 0 && !pane->shutting_down) {
        /* Negative read: the child shell exited (UV_EOF) — OR a transient tty
         * hangup the client provoked while the shell is alive. Closing the pane
         * is only correct when the child is verifiably gone; a dying child
         * closes its fds a hair before it becomes waitable, so re-poll briefly. */
        int child_alive = 0;
        if (pane->pty && pane->pty->ops->child_alive) {
            for (int attempt = 0; attempt < 64; ++attempt) {
                child_alive = pane->pty->ops->child_alive(pane->pty) == 1;
                if (!child_alive) {
                    break;
                }
            }
        }
        if (child_alive) {
            ywarn("ymux_pane: PTY read error (nread=%ld) with the child still alive — "
                  "transient tty hangup, keeping the terminal open",
                  nread);
            return;
        }
        ydebug("ymux_pane: PTY EOF (nread=%ld), notifying owner", nread);
        pane->shutting_down = 1;
        if (pane->child_exit_fn) {
            struct yetty_ycore_void_result cr = pane->child_exit_fn(pane->child_exit_userdata);
            if (YETTY_IS_ERR(cr)) {
                yetty_ycore_error_destroy(cr.error);
            }
        }
    } else {
        ydebug("ymux_pane: pty read skipped (nread=%ld)", nread);
    }
}

/* yetty_yvterm_grid_pty_write_fn impl — the grid's keyboard/query output goes
 * to this pane's PTY master. Adapts the single-shot size result to void. */
static struct yetty_ycore_void_result pane_grid_pty_write(const char *data, size_t len,
                                                          void *userdata)
{
    struct yetty_ymux_pane *pane = userdata;
    struct yetty_ycore_size_result r = yetty_ymux_pane_write(pane, data, len);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "pane_grid_pty_write: pane_write failed");
    return YETTY_OK_VOID();
}

/* yetty_ywire_connection_writer_fn impl — the host-side channel connection ships
 * its encoded frames through the PTY master (looping). */
static struct yetty_ycore_void_result pane_channel_writer(const uint8_t *bytes, size_t n,
                                                          void *userdata)
{
    struct yetty_ymux_pane *pane = userdata;
    return yetty_ymux_pane_dcs_emit(pane, bytes, n);
}

/*===========================================================================
 * Lifecycle
 *=========================================================================*/

struct yetty_ymux_pane_ptr_result yetty_ymux_pane_create(struct yetty_ycore_grid_size grid_size,
                                                         const struct yetty_context *context)
{
    uint32_t cols = grid_size.cols;
    uint32_t rows = grid_size.rows;

    if (!context) {
        return YETTY_ERR(yetty_ymux_pane_ptr, "yetty_ymux_pane_create: NULL context");
    }
    if (!context->event_loop) {
        return YETTY_ERR(yetty_ymux_pane_ptr, "yetty_ymux_pane_create: no event_loop in context");
    }

    struct yetty_yplatform_pty_factory *pty_factory = context->pty_factory;
    if (!pty_factory || !pty_factory->ops || !pty_factory->ops->create_pty) {
        return YETTY_ERR(yetty_ymux_pane_ptr,
                         "yetty_ymux_pane_create: pty_factory is NULL or has no create_pty op");
    }

    struct yetty_ymux_pane *pane = calloc(1, sizeof(struct yetty_ymux_pane));
    if (!pane) {
        return YETTY_ERR(yetty_ymux_pane_ptr, "yetty_ymux_pane_create: allocation failed");
    }
    pane->context = *context;
    pane->cols = cols;
    pane->rows = rows;

    ydebug("ymux_pane_create: cols=%u rows=%u", cols, rows);

    /* PTY. */
    struct yetty_yplatform_pty_ptr_result pty_res =
        pty_factory->ops->create_pty(pty_factory, context->event_loop);
    if (YETTY_IS_ERR(pty_res)) {
        struct yetty_ycore_void_result d = yetty_ymux_pane_destroy(pane);
        if (YETTY_IS_ERR(d)) {
            yetty_ycore_error_destroy(d.error);
        }
        return YETTY_ERR(yetty_ymux_pane_ptr, "yetty_ymux_pane_create: create_pty failed", pty_res);
    }
    pane->pty = pty_res.value;

    /* Wire statemachine — owns the PTY pointer (non-owning), decode stack, and
     * the per-OSC-code handler registry. */
    struct yetty_ywire_wire_statemachine_ptr_result sm_res =
        yetty_ywire_wire_statemachine_create(pane->pty);
    if (YETTY_IS_ERR(sm_res)) {
        struct yetty_ycore_void_result d = yetty_ymux_pane_destroy(pane);
        if (YETTY_IS_ERR(d)) {
            yetty_ycore_error_destroy(d.error);
        }
        return YETTY_ERR(yetty_ymux_pane_ptr, "yetty_ymux_pane_create: wire_statemachine_create",
                         sm_res);
    }
    pane->sm = sm_res.value;

    /* Long-lived yface for emitting input events to the inferior. */
    struct yetty_yface_ptr_result yf_res = yetty_yface_create();
    if (YETTY_IS_ERR(yf_res)) {
        struct yetty_ycore_void_result d = yetty_ymux_pane_destroy(pane);
        if (YETTY_IS_ERR(d)) {
            yetty_ycore_error_destroy(d.error);
        }
        return YETTY_ERR(yetty_ymux_pane_ptr, "yetty_ymux_pane_create: yface_create", yf_res);
    }
    pane->emit_yface = yf_res.value;

    /* Register the PTY pipe for async-delivery backends. Sync-read backends
     * (memory-pty) return NULL here; the SM pulls bytes itself on process(). */
    struct yetty_platform_pty_pipe_source *pipe_source = pane->pty->ops->pipe_source(pane->pty);
    if (pipe_source) {
        struct yetty_yevent_pipe_id_result pipe_res = context->event_loop->ops->register_pty_pipe(
            context->event_loop, pipe_source, pane_pty_pipe_alloc, pane_pty_pipe_read, pane);
        if (YETTY_IS_ERR(pipe_res)) {
            struct yetty_ycore_void_result d = yetty_ymux_pane_destroy(pane);
            if (YETTY_IS_ERR(d)) {
                yetty_ycore_error_destroy(d.error);
            }
            return YETTY_ERR(yetty_ymux_pane_ptr, "yetty_ymux_pane_create: register_pty_pipe",
                             pipe_res);
        }
        pane->pty_pipe_id = pipe_res.value;
    }

    /* The bare model — libvterm text grid + ydraw canvas, GPU-free. */
    struct yetty_yconfig_config *config = context->runtime ? context->runtime->config : NULL;
    uint32_t scrollback_rows =
        config ? (uint32_t)config->ops->get_int(config, YETTY_YCONFIG_KEY_SCROLLBACK_LINES, 10000)
               : 0u;
    uint32_t hot_rows =
        config
            ? (uint32_t)config->ops->get_int(config, YETTY_YCONFIG_KEY_SCROLLBACK_HOT_LINES, 2000)
            : 0u;
    struct yetty_yclass_object_ptr_result grid_res =
        yetty_yvterm_grid_make(cols, rows, scrollback_rows, hot_rows);
    if (YETTY_IS_ERR(grid_res)) {
        struct yetty_ycore_void_result d = yetty_ymux_pane_destroy(pane);
        if (YETTY_IS_ERR(d)) {
            yetty_ycore_error_destroy(d.error);
        }
        return YETTY_ERR(yetty_ymux_pane_ptr, "yetty_ymux_pane_create: grid_make", grid_res);
    }
    pane->grid = grid_res.value;

    /* Warm/cold tier budgets + memtag accounting (best-effort). */
    if (config) {
        uint32_t warm_bytes =
            (uint32_t)config->ops->get_int(config, YETTY_YCONFIG_KEY_SCROLLBACK_WARM_BYTES, 0);
        uint32_t file_max_bytes =
            (uint32_t)config->ops->get_int(config, YETTY_YCONFIG_KEY_SCROLLBACK_FILE_MAX_BYTES, 0);
        struct yetty_ycore_void_result br =
            yetty_yvterm_grid_set_tier_budgets(pane->grid, warm_bytes, file_max_bytes);
        if (YETTY_IS_ERR(br)) {
            yetty_ycore_error_destroy(br.error);
        }
    }
    if (context->runtime && context->runtime->memtag_registry) {
        struct yetty_ycore_void_result mr =
            yetty_yvterm_grid_register_memtags(pane->grid, context->runtime->memtag_registry);
        if (YETTY_IS_ERR(mr)) {
            yetty_ycore_error_destroy(mr.error);
        }
    }

    /* Grid keyboard/query output → this pane's PTY. */
    struct yetty_ycore_void_result pw_res =
        yetty_yvterm_grid_set_pty_write(pane->grid, pane_grid_pty_write, pane);
    if (YETTY_IS_ERR(pw_res)) {
        struct yetty_ycore_void_result d = yetty_ymux_pane_destroy(pane);
        if (YETTY_IS_ERR(d)) {
            yetty_ycore_error_destroy(d.error);
        }
        return YETTY_ERR(yetty_ymux_pane_ptr, "yetty_ymux_pane_create: grid_set_pty_write", pw_res);
    }

    /* Bind the grid as the statemachine's default (raw-passthrough) sink. */
    struct yetty_ycore_void_result rw_res = yetty_yvterm_grid_register_wire(pane->grid, pane->sm);
    if (YETTY_IS_ERR(rw_res)) {
        struct yetty_ycore_void_result d = yetty_ymux_pane_destroy(pane);
        if (YETTY_IS_ERR(d)) {
            yetty_ycore_error_destroy(d.error);
        }
        return YETTY_ERR(yetty_ymux_pane_ptr, "yetty_ymux_pane_create: grid_register_wire", rw_res);
    }

    /* Host-side connection layer — the acceptor of dynamic ywire channels opened
     * by in-pane clients. Rides the same statemachine + PTY-master writer. With
     * no accept callback every OPEN is answered with CLOSE. */
    struct yetty_ywire_connection_ptr_result ch_res =
        yetty_ywire_connection_attach(pane->sm, pane_channel_writer, pane, /*compressed=*/0);
    if (YETTY_IS_ERR(ch_res)) {
        struct yetty_ycore_void_result d = yetty_ymux_pane_destroy(pane);
        if (YETTY_IS_ERR(d)) {
            yetty_ycore_error_destroy(d.error);
        }
        return YETTY_ERR(yetty_ymux_pane_ptr, "yetty_ymux_pane_create: connection_attach", ch_res);
    }
    pane->channel_host = ch_res.value;

    ydebug("ymux_pane_create: ready");
    return YETTY_OK(yetty_ymux_pane_ptr, pane);
}

struct yetty_ycore_void_result yetty_ymux_pane_destroy(struct yetty_ymux_pane *pane)
{
    if (!pane) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ymux_pane_destroy: NULL pane");
    }

    /* Best-effort: run every step, stash the first error, surface it at the end. */
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    bool have_err = false;

    /* Destroy the statemachine BEFORE the pty (holds a non-owning pty pointer)
     * and before the channel host (the SM's handler points at it). */
    if (pane->sm) {
        struct yetty_ycore_void_result r = yetty_ywire_wire_statemachine_destroy(pane->sm);
        if (YETTY_IS_ERR(r)) {
            if (!have_err) {
                first_err = r;
                have_err = true;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
        pane->sm = NULL;
    }

    /* Channel host — attach-mode connection; only safe to free after the SM. */
    if (pane->channel_host) {
        struct yetty_ycore_void_result r = yetty_ywire_connection_destroy(pane->channel_host);
        if (YETTY_IS_ERR(r)) {
            if (!have_err) {
                first_err = r;
                have_err = true;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
        pane->channel_host = NULL;
    }

    /* The grid model (the pane owns it; no renderer figure disposes it). */
    if (pane->grid) {
        struct yetty_ycore_void_result r = yetty_yvterm_grid_dispose(pane->grid);
        if (YETTY_IS_ERR(r)) {
            if (!have_err) {
                first_err = r;
                have_err = true;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
        pane->grid = NULL;
    }

    if (pane->pty && pane->pty->ops && pane->pty->ops->destroy) {
        struct yetty_ycore_void_result r = pane->pty->ops->destroy(pane->pty);
        if (YETTY_IS_ERR(r)) {
            if (!have_err) {
                first_err = r;
                have_err = true;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
    }
    pane->pty = NULL;

    if (pane->emit_yface) {
        yetty_yface_destroy(pane->emit_yface);
        pane->emit_yface = NULL;
    }

    free(pane->pty_read_buf);
    free(pane);

    if (have_err) {
        return YETTY_ERR(yetty_ycore_void,
                         "yetty_ymux_pane_destroy: one or more cleanup steps failed", first_err);
    }
    return YETTY_OK_VOID();
}

/*===========================================================================
 * Accessors + operations
 *=========================================================================*/

struct yetty_yclass_object *yetty_ymux_pane_grid(struct yetty_ymux_pane *pane)
{
    return pane ? pane->grid : NULL;
}

struct yetty_ywire_wire_statemachine *yetty_ymux_pane_sm(struct yetty_ymux_pane *pane)
{
    return pane ? pane->sm : NULL;
}

struct yetty_platform_pty *yetty_ymux_pane_pty(struct yetty_ymux_pane *pane)
{
    return pane ? pane->pty : NULL;
}

struct yetty_ywire_connection *yetty_ymux_pane_channel_host(struct yetty_ymux_pane *pane)
{
    return pane ? pane->channel_host : NULL;
}

struct yetty_yface *yetty_ymux_pane_emit_yface(struct yetty_ymux_pane *pane)
{
    return pane ? pane->emit_yface : NULL;
}

uint32_t yetty_ymux_pane_cols(const struct yetty_ymux_pane *pane)
{
    return pane ? pane->cols : 0;
}

uint32_t yetty_ymux_pane_rows(const struct yetty_ymux_pane *pane)
{
    return pane ? pane->rows : 0;
}

struct yetty_ycore_size_result yetty_ymux_pane_write(struct yetty_ymux_pane *pane, const char *data,
                                                     size_t len)
{
    if (!pane || !pane->pty || !pane->pty->ops->write) {
        return YETTY_ERR(yetty_ycore_size, "yetty_ymux_pane_write: PTY backend has no `write` op");
    }
    return pane->pty->ops->write(pane->pty, data, len);
}

struct yetty_ycore_void_result yetty_ymux_pane_dcs_emit(struct yetty_ymux_pane *pane,
                                                        const uint8_t *bytes, size_t n)
{
    size_t off = 0;
    while (off < n) {
        struct yetty_ycore_size_result wr =
            yetty_ymux_pane_write(pane, (const char *)bytes + off, n - off);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "yetty_ymux_pane_dcs_emit: pane_write failed");
        if (wr.value == 0) {
            /* EAGAIN territory — back off so we don't spin while the kernel
             * drains the PTY buffer. */
            yetty_yplatform_ytime_sleep_ms(1);
            continue;
        }
        off += wr.value;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ymux_pane_resize_pty(struct yetty_ymux_pane *pane,
                                                          struct yetty_ycore_grid_size grid_size,
                                                          struct yetty_ycore_pixel_size cell_size)
{
    if (!pane) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ymux_pane_resize_pty: NULL pane");
    }
    pane->cols = grid_size.cols;
    pane->rows = grid_size.rows;
    if (pane->pty && pane->pty->ops && pane->pty->ops->resize) {
        struct yetty_ycore_void_result pr = pane->pty->ops->resize(
            pane->pty, grid_size.cols, grid_size.rows, grid_size.cols * (uint32_t)cell_size.width,
            grid_size.rows * (uint32_t)cell_size.height);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "yetty_ymux_pane_resize_pty: pty resize failed");
    }
    return YETTY_OK_VOID();
}

void yetty_ymux_pane_set_notify(struct yetty_ymux_pane *pane, yetty_ymux_pane_notify_fn fn,
                                void *userdata)
{
    if (!pane) {
        return;
    }
    pane->notify_fn = fn;
    pane->notify_userdata = userdata;
}

void yetty_ymux_pane_set_child_exit(struct yetty_ymux_pane *pane, yetty_ymux_pane_child_exit_fn fn,
                                    void *userdata)
{
    if (!pane) {
        return;
    }
    pane->child_exit_fn = fn;
    pane->child_exit_userdata = userdata;
}

void yetty_ymux_pane_set_output_tap(struct yetty_ymux_pane *pane, yetty_ymux_pane_output_tap_fn fn,
                                    void *userdata)
{
    if (!pane) {
        return;
    }
    pane->output_tap_fn = fn;
    pane->output_tap_userdata = userdata;
}

int yetty_ymux_pane_is_shutting_down(const struct yetty_ymux_pane *pane)
{
    return pane ? pane->shutting_down : 1;
}

void yetty_ymux_pane_set_shutting_down(struct yetty_ymux_pane *pane, int value)
{
    if (pane) {
        pane->shutting_down = value;
    }
}
