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
struct yetty_ymgui_hit {
    uint32_t figure_id;
    float local_x;
    float local_y;
};

/* Callback the compositor uses to ship server-to-client ymgui OSCs
 * (YMGUI_OSC_SC_RESIZE / SC_FOCUS) back to the client process. The
 * compositor passes the OSC code + raw payload bytes; the host wraps
 * them in a yface envelope and writes to the PTY. */
typedef struct yetty_ycore_void_result (*yetty_ymgui_emit_osc_fn)(
    int osc_code, const void *data, size_t size, void *user);

/* Callback the compositor fires when a YMGUI_OSC_CS_TERM_INPUT_SUB
 * envelope arrives. Updates the host's terminal-wide input subscription
 * bitmask. Returns a Result so the host can fail back to the compositor
 * (e.g. emit_yface failed shipping a TERM_RESIZE on rising edge). */
typedef struct yetty_ycore_void_result (*yetty_ymgui_term_input_sub_fn)(
    uint32_t flags, void *user);

/* Shared pipeline lifecycle. The compositor (or any host that owns
 * multiple ymgui figures) builds one lazily on first ymgui OSC and
 * hands the borrowed pointer to every figure it creates. The
 * pipeline must outlive every figure that holds the pointer. The
 * internal layout is private to the ymgui module — callers treat
 * `struct yetty_ymgui_pipeline *` as opaque. */
struct yetty_ymgui_pipeline_ptr_result yetty_ymgui_pipeline_create(
    const struct yetty_context *context);

struct yetty_ycore_void_result yetty_ymgui_pipeline_destroy(
    struct yetty_ymgui_pipeline *pipeline);

/* Create at `rect` (absolute target pixel space). The pipeline pointer
 * is borrowed and must outlive the figure. Frame + atlas start empty;
 * the figure renders nothing until both have been set at least once. */
struct yetty_ymgui_figure_ptr_result yetty_ymgui_figure_create(
    struct yetty_ycore_rectangle rect,
    struct yetty_ymgui_pipeline *pipeline,
    const struct yetty_context *context);

/* Upcast. Stable pointer. */
struct yetty_yfigure_figure *yetty_ymgui_figure_as_figure(
    struct yetty_ymgui_figure *figure);

/* Replace the decoded ImGui frame. Expects the wire shape documented
 * in <yetty/ymgui/wire.h> (frame_header + per-cmd-list mesh). Bytes
 * are copied; the figure marks itself dirty so the compositor repaints
 * the rect next pass. */
struct yetty_ycore_void_result yetty_ymgui_figure_set_frame(
    struct yetty_ymgui_figure *figure,
    const uint8_t *frame_bytes, size_t frame_size);

/* Replace the font atlas (R8 today; matches ImGui's Alpha8 atlas).
 * Pixel data is copied to a fresh WGPUTexture on next render. */
struct yetty_ycore_void_result yetty_ymgui_figure_set_atlas(
    struct yetty_ymgui_figure *figure,
    const uint8_t *atlas_bytes, size_t atlas_size,
    uint32_t atlas_w, uint32_t atlas_h);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YMGUI_FIGURE_H */
