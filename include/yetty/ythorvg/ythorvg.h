#ifndef YETTY_YTHORVG_H
#define YETTY_YTHORVG_H

/*
 * ythorvg - ThorVG SVG/Lottie renderer emitting into a ydraw core buffer.
 *
 * Wraps a C++ tvg::RenderMethod whose shape/image prepare+render paths are
 * mapped to yetty_ydraw_drawable_list_add_cmd_add_* primitives written into a
 * yetty_ydraw_drawable_list. The C++ implementation is ported from
 * yetty-poc's src/yetty/ythorvg with YDrawBuffer calls replaced by
 * ydraw/ysdf buffer calls.
 *
 * Engine init/term is reference-counted; create/destroy pairs handle it.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;
struct yetty_ythorvg_renderer;

YETTY_YRESULT_DECLARE(yetty_ythorvg_renderer_ptr, struct yetty_ythorvg_renderer *);

/* Create a renderer that emits primitives into `buf`. The buffer must outlive
 * the renderer; the renderer does NOT take ownership. Initializes the ThorVG
 * engine on first call (ref-counted). */
struct yetty_ythorvg_renderer_ptr_result yetty_ythorvg_renderer_create(
    struct yetty_ydraw_drawable_list *buf);

/* Destroy renderer. NULL-safe. Decrements engine ref count; calls
 * tvg::Initializer::term() when the last renderer is destroyed. */
void yetty_ythorvg_renderer_destroy(struct yetty_ythorvg_renderer *r);

/* Set target viewport size (used for bounds; does not affect culling). */
void yetty_ythorvg_renderer_set_target(struct yetty_ythorvg_renderer *r, uint32_t width,
                                       uint32_t height);

/* Load + render a single frame from `data` (SVG text or Lottie JSON) into the
 * buffer. `mimetype` may be "svg", "lottie", or NULL for auto-detect. On
 * success, *out_width and *out_height (if non-NULL) receive the content's
 * intrinsic size. For Lottie animations this renders frame 0; use
 * yetty_ythorvg_render_frame to advance. */
struct yetty_ycore_void_result yetty_ythorvg_renderer_render(struct yetty_ythorvg_renderer *r,
                                                             const void *data, size_t size,
                                                             const char *mimetype, float *out_width,
                                                             float *out_height);

/* Advance a previously-loaded Lottie animation to `frame` (0..total_frames)
 * and re-render into the buffer. No-op if no animation is loaded. */
struct yetty_ycore_void_result yetty_ythorvg_renderer_render_frame(struct yetty_ythorvg_renderer *r,
                                                                   float frame);

/* Query the last-loaded animation's frame count / duration. Returns 0 if no
 * animation is loaded or the content is a static SVG. */
float yetty_ythorvg_renderer_total_frames(const struct yetty_ythorvg_renderer *r);
float yetty_ythorvg_renderer_duration(const struct yetty_ythorvg_renderer *r);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YTHORVG_H */
