#ifndef YETTY_YDIAGRAM_RENDERER_H
#define YETTY_YDIAGRAM_RENDERER_H

/*
 * renderer — emit a laid-out diagram graph into a ydraw buffer using
 * MSD (SDF shape) and MSDF (text) primitives.
 *
 * Caller owns the ydraw buffer; we only append primitives. Scene bounds
 * on the buffer are set from the graph's computed bounds.
 *
 * For text measurement (used to centre labels), the renderer accepts the
 * same callback signature as the layout engine. Passing NULL gives a crude
 * 0.6 * font_size * len fallback — adequate for diagnostics, ugly for
 * production.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ydiagram/graph-ir.h>
#include <yetty/ydiagram/layout.h>
#include <yetty/ydraw-core/draw-list.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydiagram_render_options {
    float arrow_size;          /* px, default 8 */
    float dash_length;         /* px, default 6 */
    float dash_gap;            /* px, default 4 */
    uint32_t background_color; /* 0 = transparent; otherwise emit a big box */
};

struct yetty_ydiagram_render_options yetty_ydiagram_default_render_options(void);

struct yetty_ycore_void_result yetty_ydiagram_render(
    const struct yetty_ydiagram_graph *g, struct yetty_ydraw_draw_list *buffer,
    const struct yetty_ydiagram_render_options *options, yetty_ydiagram_measure_text_fn measure,
    void *measure_userdata);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDIAGRAM_RENDERER_H */
