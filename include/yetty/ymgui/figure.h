/*
 * yetty_ymgui_figure — one Dear-ImGui frame as a compositor figure.
 *
 * Subclass of yetty_yfigure_figure. Replaces the per-card slot in the
 * old ymgui terminal layer: a single ImGui-app instance positioned in
 * absolute target pixel space. Move/resize is by the compositor calling
 * yfigure container set_rect — there is no rolling-row anchor,
 * no col/row, no cell math.
 *
 * Owned state: decoded ImGui frame bytes (per the wire format in
 * <yetty/ymgui/wire.h>) + font atlas + per-instance GPU buffers
 * (vertex/index/uniform/bind group + atlas texture/view).
 *
 * Borrowed: a yetty_ymgui_pipeline pointer for the shared shader +
 * sampler + render pipeline. The pipeline must outlive every figure.
 *
 * Render coordinate model:
 *   The frame's vertex coords are in frame-local pixels (origin at the
 *   frame's top-left, matching ImGui DisplayPos=(0,0) /
 *   DisplaySize=(frame_w, frame_h)). At render time the figure passes
 *   `display_size = (frame_w, frame_h)` from the frame header and
 *   `frame_top = figure->rect.min` so the shader places the frame at
 *   the figure's absolute rect. Move = just changes the origin uniform.
 *   Resize = viewport grows/shrinks; the frame keeps its authored size
 *   until the client ships a fresh frame matching the new pixel rect.
 */
#ifndef YETTY_YMGUI_FIGURE_H
#define YETTY_YMGUI_FIGURE_H

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

struct yetty_ymgui_figure;
struct yetty_ymgui_pipeline;

YETTY_YRESULT_DECLARE(yetty_ymgui_figure_ptr, struct yetty_ymgui_figure *);
YETTY_YRESULT_DECLARE(yetty_ymgui_pipeline_ptr, struct yetty_ymgui_pipeline *);

/* Result of a hit-test against the compositor's live ymgui figures.
 * figure_id == 0 means no figure was under the queried point. When
 * figure_id != 0, local_x/local_y are the cursor's coordinates in the
 * figure's own pixel space (origin = figure's top-left). */
/* Callback the compositor uses to ship server-to-client ymgui OSCs
 * (YETTY_OSC_SC_CLIENT_INPUT_FIGURE_RESIZE / SC_FOCUS) back to the client process. The
 * compositor passes the OSC code + raw payload bytes; the host wraps
 * them in a yface envelope and writes to the PTY. */
typedef struct yetty_ycore_void_result (*yetty_ymgui_emit_osc_fn)(int osc_code, const void *data,
                                                                  size_t size, void *user);

/* Callback the compositor fires when a YETTY_OSC_CS_CLIENT_INPUT_SUB
 * envelope arrives. Updates the host's terminal-wide input subscription
 * bitmask. Returns a Result so the host can fail back to the compositor
 * (e.g. emit_yface failed shipping a TERM_RESIZE on rising edge). */
typedef struct yetty_ycore_void_result (*yetty_ymgui_term_input_sub_fn)(uint32_t flags, void *user);

/* Shared pipeline lifecycle. The compositor (or any host that owns
 * multiple ymgui figures) builds one lazily on first ymgui OSC and
 * hands the borrowed pointer to every figure it creates. The
 * pipeline must outlive every figure that holds the pointer. The
 * internal layout is private to the ymgui module — callers treat
 * `struct yetty_ymgui_pipeline *` as opaque. */
struct yetty_ymgui_pipeline_ptr_result yetty_ymgui_pipeline_create(
    const struct yetty_context *context);

struct yetty_ycore_void_result yetty_ymgui_pipeline_destroy(struct yetty_ymgui_pipeline *pipeline);

/* Create at `rect` (absolute target pixel space). The pipeline pointer
 * is borrowed and must outlive the figure. Frame + atlas start empty;
 * the figure renders nothing until both have been set at least once. */
struct yetty_ymgui_figure_ptr_result yetty_ymgui_figure_create(
    struct yetty_ycore_rectangle rect, struct yetty_ymgui_pipeline *pipeline,
    const struct yetty_context *context);

/* Upcast. Stable pointer. */
struct yetty_yfigure_figure *yetty_ymgui_figure_as_figure(struct yetty_ymgui_figure *figure);

/* Replace the decoded ImGui frame. Expects the wire shape documented
 * in <yetty/ymgui/wire.h> (frame_header + per-cmd-list mesh). Bytes
 * are copied; the figure marks itself dirty so the compositor repaints
 * the rect next pass. */
struct yetty_ycore_void_result yetty_ymgui_figure_set_frame(struct yetty_ymgui_figure *figure,
                                                            const uint8_t *frame_bytes,
                                                            size_t frame_size);

/* Replace the font atlas (R8 today; matches ImGui's Alpha8 atlas).
 * Pixel data is copied to a fresh WGPUTexture on next render. */
struct yetty_ycore_void_result yetty_ymgui_figure_set_atlas(struct yetty_ymgui_figure *figure,
                                                            const uint8_t *atlas_bytes,
                                                            size_t atlas_size, uint32_t atlas_w,
                                                            uint32_t atlas_h);

/*===========================================================================
 * Figure-kind factory registration.
 *
 * The host (terminal, yui) hands a non-NULL `factory_args` to the registry
 * via `yetty_ymgui_register_factory`. The args struct outlives every
 * minted figure — the registry borrows it. The pipeline is built lazily
 * on the first mint and stored on the args; the host releases it at
 * shutdown with `yetty_ymgui_factory_args_release`.
 *
 * The args also expose the shared pipeline so the host can hand it to
 * any code that needs to render ymgui content outside the figure-tree
 * path (in-process embedded mode, tests).
 *=========================================================================*/

struct yetty_ymgui_factory_args {
    /* Borrowed — used to lazily build `pipeline` on first mint. The
     * underlying context (GPU device / queue / config) must outlive the
     * args struct. */
    const struct yetty_context *context;

    /* Owned by the args once built. NULL until the first factory mint
     * triggers `yetty_ymgui_pipeline_create`. Host MUST call
     * `yetty_ymgui_factory_args_release` at shutdown to free it. */
    struct yetty_ymgui_pipeline *pipeline;
};

/* Register the YMGUI factory under YETTY_YFIGURE_KIND_YMGUI. `args` is
 * borrowed; the host owns its lifetime and MUST keep it alive while the
 * registry references it. Errors if the kind is already registered. */
struct yetty_ycore_void_result yetty_ymgui_register_factory(struct yetty_yfigure_registry *registry,
                                                            struct yetty_ymgui_factory_args *args);

/* Tear down the lazily-built pipeline (if any). Safe to call when no
 * pipeline was ever built; in that case the args are simply zeroed. */
struct yetty_ycore_void_result yetty_ymgui_factory_args_release(
    struct yetty_ymgui_factory_args *args);

/* Downcast helper. Returns the typed pointer when `base` is actually
 * an ymgui figure (identified by its ops vtable), NULL otherwise. Use
 * this to filter the heterogeneous children of a yfigure container. */
struct yetty_ymgui_figure *yetty_ymgui_figure_from_base(struct yetty_yfigure_figure *base);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMGUI_FIGURE_H */
