/* GENERATED — do not edit. */
/* Public interface for regular class(es) `figure` (module: yrdawn).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YRDAWN_FIGURE_H
#define YETTY_YCLASSGEN_YRDAWN_FIGURE_H

#include <yetty/yclass/class.h>
#include <yetty/yrdawn/methods.h>

struct yetty_yclass_ptr_result yetty_yrdawn_figure_class_get(void);

/* Header-destined content for the generated figure.h (skipped by the real build, which takes it from that header). */
/*
 * yetty_yrdawn_figure — one remote yrdawn canvas as a compositor figure.
 *
 * Subclass of yetty_yfigure_figure. Each figure represents one
 * canvas the wasm client opened against this terminal. Multiple
 * canvases may coexist; they share a yetty_yrdawn_session keyed by
 * the client-chosen session_id (one session per wasm process).
 *
 * The figure owns:
 *   - a small textured-quad pipeline that samples its frame texture;
 *   - the WGPUTexture / view / bind group that holds the most recent
 *     presented frame;
 *   - its `figure_id` (== record id on the wire == canvas address);
 *   - a borrowed pointer to its session;
 *   - a borrowed pointer to factory_args.
 *
 * Construction goes through the codegen yclass factory
 * (yetty_yrdawn_figure_create); the registry factory wrapper
 * yetty_yrdawn_factory mints one and binds its per-instance state.
 */

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
 * to each host's registry.
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

/* Downcast helper. Returns NULL when `base` isn't a yrdawn figure. */
struct yetty_yrdawn_figure *yetty_yrdawn_figure_from_base(struct yetty_yfigure_figure *base);

#ifdef __cplusplus
}
#endif

#endif
