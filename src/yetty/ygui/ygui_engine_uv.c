/*
 * ygui_engine_uv.c — libuv-driven event-loop integration for ygui.
 *
 * Lives in the `ygui` static library (layered on top of `ygui-core`).
 * ygui-core itself never references libuv: the engine struct carries
 * an opaque `void *uv_state` + a destroy hook (see ygui_internal.h),
 * which this file populates when a caller invokes
 * yetty_ygui_engine_attach / _run / _poll / _get_loop.
 *
 * Standalone tools that drive their own UI (ygreeter, ytop, …) link
 * `ygui` (full) and call yetty_ygui_engine_run. Embedders / cross
 * targets where libuv isn't a fit (webasm) link `ygui-core` alone —
 * the engine constructs and destroys cleanly without ever entering
 * the libuv code path.
 */

#include "ygui_internal.h"
#include <yetty/yface/yface.h>
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

/*-----------------------------------------------------------------------------
 * Opaque libuv state stored in engine->uv_state.
 *---------------------------------------------------------------------------*/

struct ygui_uv_state {
    uv_loop_t *loop;
    int        owns_loop;
    uv_poll_t  stdin_poll;
    uv_prepare_t prepare_handle;
};

static struct ygui_uv_state *uv_state_of(struct yetty_ygui_engine *engine)
{
    return engine ? (struct ygui_uv_state *)engine->uv_state : NULL;
}

static void uv_state_destroy(struct yetty_ygui_engine *engine)
{
    struct ygui_uv_state *s = uv_state_of(engine);
    if (!s) {
        return;
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
    struct yetty_ygui_engine *engine = (struct yetty_ygui_engine *)handle->data;
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
                yetty_ygui_internal_process_input(engine, buf, (int)n);
            }
        } else if (n == 0) {
            /* EOF */
            engine->running = 0;
        }
    }
}

YETTY_EXTERNAL_CALLBACK
static void prepare_cb(uv_prepare_t *handle)
{
    struct yetty_ygui_engine *engine = (struct yetty_ygui_engine *)handle->data;
    struct ygui_uv_state *s = uv_state_of(engine);

    /* Check for terminal resize */
    if (yetty_ygui_internal_resize_pending) {
        yetty_ygui_internal_resize_pending = 0;
#ifndef _WIN32
        /* Pick up the new host-terminal cell count. Without this the card
         * stays at its original cell dims, the cell pixel size also stays
         * (no zoom), and handle_resize ends up with no delta — i.e. the
         * resize event fires but nothing actually changes. */
        struct winsize ws;
        if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
            int new_w = ws.ws_col;
            int new_h = ws.ws_row;
            if (new_w != engine->card_w || new_h != engine->card_h) {
                engine->card_w = new_w;
                engine->card_h = new_h;
                /* Re-place the card at the new cell size — yetty re-tiles
                 * and sends OSC 777780 with the new pixel size, which the
                 * core engine handles via needs_resize / dirty flags. */
                struct yetty_ycore_void_result pr =
                    yetty_ygui_osc_card_place(engine->card_id, engine->card_x, engine->card_y,
                                              (uint32_t)new_w, (uint32_t)new_h);
                if (YETTY_IS_ERR(pr)) {
                    yetty_ycore_error_destroy(pr.error);
                }
            }
        }
#endif
        yetty_ygui_osc_query_cell_size();
    }

    /* Auto-render if dirty */
    if (engine->dirty) {
        yetty_ygui_engine_render(engine);
    }

    /* Check if we should stop */
    if (!engine->running && s && s->loop) {
        uv_stop(s->loop);
    }
}

/*-----------------------------------------------------------------------------
 * Public API
 *---------------------------------------------------------------------------*/

void yetty_ygui_engine_attach(struct yetty_ygui_engine *engine, uv_loop_t *loop)
{
    if (!engine || !loop) {
        return;
    }

    /* Lazily allocate the uv state on first attach. yetty_ygui_engine_run
     * may have already populated it (with owns_loop=1) — in which case
     * we just re-use that allocation. */
    struct ygui_uv_state *s = uv_state_of(engine);
    if (!s) {
        s = (struct ygui_uv_state *)calloc(1, sizeof(*s));
        if (!s) {
            return;
        }
        engine->uv_state = s;
        engine->uv_state_destroy_cb = uv_state_destroy;
    }
    s->loop = loop;
    /* owns_loop stays whatever the caller already set — see engine_run. */

    /* Wire up the yface handlers so feed_bytes can dispatch into us. */
    if (engine->yface_in) {
        yetty_yface_set_handlers(engine->yface_in,
                                 yetty_ygui_internal_yface_on_osc,
                                 yetty_ygui_internal_yface_on_raw,
                                 engine);
    }

    /* Set up stdin poll */
    uv_poll_init(loop, &s->stdin_poll, engine->input_fd);
    s->stdin_poll.data = engine;
    uv_poll_start(&s->stdin_poll, UV_READABLE, stdin_poll_cb);

    /* Set up prepare handle for auto-render */
    uv_prepare_init(loop, &s->prepare_handle);
    s->prepare_handle.data = engine;
    uv_prepare_start(&s->prepare_handle, prepare_cb);
}

void yetty_ygui_engine_run(struct yetty_ygui_engine *engine)
{
    if (!engine) {
        return;
    }

    struct ygui_uv_state *s = uv_state_of(engine);
    if (!s) {
        s = (struct ygui_uv_state *)calloc(1, sizeof(*s));
        if (!s) {
            return;
        }
        engine->uv_state = s;
        engine->uv_state_destroy_cb = uv_state_destroy;
    }

    /* Create loop if needed */
    if (!s->loop) {
        s->loop = (uv_loop_t *)malloc(sizeof(uv_loop_t));
        if (!s->loop) {
            return;
        }
        uv_loop_init(s->loop);
        s->owns_loop = 1;

        /* Attach to the loop */
        yetty_ygui_engine_attach(engine, s->loop);
    }

    engine->running = 1;

    /* Run the loop */
    uv_run(s->loop, UV_RUN_DEFAULT);

    /* Cleanup handles */
    uv_poll_stop(&s->stdin_poll);
    uv_prepare_stop(&s->prepare_handle);
}

uv_loop_t *yetty_ygui_engine_get_loop(struct yetty_ygui_engine *engine)
{
    struct ygui_uv_state *s = uv_state_of(engine);
    return s ? s->loop : NULL;
}

int yetty_ygui_engine_poll(struct yetty_ygui_engine *engine)
{
    struct ygui_uv_state *s = uv_state_of(engine);
    if (!s || !s->loop) {
        return 0;
    }
    return uv_run(s->loop, UV_RUN_NOWAIT);
}
