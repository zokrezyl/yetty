/*
 * yetty_yrdawn_figure — one remote yrdawn canvas as a compositor figure.
 *
 * Subclass of yetty_yfigure_figure. Each figure represents one
 * canvas the wasm client opened against this terminal. Multiple
 * canvases may coexist; they share a yetty_yrdawn_session keyed by
 * the client-chosen session_id (one session per wasm process).
 *
 * The figure owns:
 *   - a small textured-quad pipeline that samples its frame texture
 *     (per-canvas — building it twice is cheap and keeps each figure
 *     self-contained);
 *   - the WGPUTexture / view / bind group that holds the most recent
 *     presented frame;
 *   - its `figure_id` (== record id on the wire == canvas address);
 *   - a borrowed pointer to its session (shared WGPU handle table,
 *     BULK reassembly slots);
 *   - a borrowed pointer to factory_args (outbound emit_osc and
 *     request_render callbacks installed by the host).
 *
 * Wire:
 *   - CREATE_CHILD admin record arrives at the root container with
 *     kind=YRDAWN, child_id, rect, and init_payload = {u32
 *     SUB_HELLO} + struct yetty_yrdawn_wire_hello. The factory
 *     mints the figure (no session yet); the container calls
 *     process_bytes(init_payload) which reads SUB_HELLO, looks up /
 *     creates the session, binds figure↔session, sends HELLO_ACK.
 *   - Subsequent records arrive on the SM and reach the figure's
 *     process_input coroutine. Each begins with u32 SUB_OP discriminator
 *     (CMD / BULK / BYE). The figure decodes the matching wire struct,
 *     runs the codegen dispatcher (CMD) or feeds the session's BULK
 *     reassembler.
 *
 * Outbound:
 *   - HELLO_ACK / REPLY / EVENT / SC_KEY / SC_RESIZE go out via the
 *     factory_args' emit_osc callback. Every outbound struct carries
 *     this figure's figure_id so the client demuxes correctly.
 */
#ifndef YETTY_YRDAWN_FIGURE_H
#define YETTY_YRDAWN_FIGURE_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yrdawn_figure;
struct yetty_yrdawn_session;
struct yetty_yfigure_registry;

YETTY_YRESULT_DECLARE(yetty_yrdawn_figure_ptr, struct yetty_yrdawn_figure *);

/* Outbound OSC emit. The figure passes its self-tagged payload (which
 * already includes its figure_id in the wire struct), the host wraps
 * it in a yface envelope and writes to the PTY. */
typedef struct yetty_ycore_void_result (*yetty_yrdawn_emit_osc_fn)(int osc_code,
                                                                   const void *payload, size_t len,
                                                                   void *user);

/* Optional repaint nudge — called when set_frame lands so the
 * compositor schedules a render pass without waiting for the next
 * polling tick. NULL is fine; dirty bit alone drives eventual repaint. */
typedef struct yetty_ycore_void_result (*yetty_yrdawn_request_render_fn)(void *user);

/*===========================================================================
 * Factory + registration
 *
 * The host (terminal, yui, …) builds one yetty_yrdawn_factory_args
 * bundle, registers it under YETTY_YFIGURE_KIND_YRDAWN, and the
 * yframework's register_figure_factories propagates the registration
 * to each host's registry. The args hold the per-host outbound
 * callbacks and the session table; both are shared across every
 * yrdawn canvas this host serves.
 *=========================================================================*/

struct yetty_yrdawn_factory_state; /* opaque — defined in figure.c */

struct yetty_yrdawn_factory_args {
    /* Borrowed — used for GPU bring-up and event loop reach. */
    const struct yetty_context *context;

    /* Outbound. emit_osc_fn MUST be set before the first figure
     * processes its HELLO; the factory copies the pair onto each
     * minted figure. */
    yetty_yrdawn_emit_osc_fn emit_osc_fn;
    void *emit_osc_user;

    /* Repaint nudge — same lifecycle as emit. NULL is acceptable. */
    yetty_yrdawn_request_render_fn request_render_fn;
    void *request_render_user;

    /* Session table — owned by the args. Lazily allocated on first
     * SUB_HELLO. `_factory_args_release` tears it down. */
    struct yetty_yrdawn_factory_state *state;
};

/* Register the YRDAWN factory under YETTY_YFIGURE_KIND_YRDAWN.
 * `args` is borrowed; the host owns its lifetime. */
struct yetty_ycore_void_result yetty_yrdawn_register_factory(
    struct yetty_yfigure_registry *registry, struct yetty_yrdawn_factory_args *args);

/* Tear down any sessions the factory minted. Safe to call when no
 * canvas was ever created. */
struct yetty_ycore_void_result yetty_yrdawn_factory_args_release(
    struct yetty_yrdawn_factory_args *args);

/* Upcast. Stable pointer. */
struct yetty_yfigure_figure *yetty_yrdawn_figure_as_figure(struct yetty_yrdawn_figure *figure);

/* Downcast helper. Returns NULL when `base` isn't a yrdawn figure
 * (identified by ops vtable identity). */
struct yetty_yrdawn_figure *yetty_yrdawn_figure_from_base(struct yetty_yfigure_figure *base);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YRDAWN_FIGURE_H */
