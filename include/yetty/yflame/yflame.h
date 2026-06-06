#ifndef YETTY_YFLAME_YFLAME_H
#define YETTY_YFLAME_YFLAME_H

/*
 * yflame — flame-graph figure.
 *
 * Ingests folded-stack text (the Brendan Gregg / `stackcollapse-*` lingua
 * franca: one `frame1;frame2;frame3 <count>` line per collapsed stack),
 * builds the call tree, lays it out as nested rectangles (width proportional
 * to sample count, y by stack depth), and emits a ydraw drawable list of
 * filled boxes + MSDF text labels — the same primitive path ydiagram uses.
 *
 * It is intentionally NOT built on yplot: a flame graph is a labelled tree of
 * rectangles, not a continuous f(x) curve. See issue tracker for the
 * why-not-yplot rationale.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Draw a truncated function-name label inside each box. */
#define YETTY_YFLAME_FLAG_LABELS 0x1u
/* Icicle orientation: root at the top, stacks growing downward. Without it
 * the classic flame orientation is used (root at the bottom, growing up). */
#define YETTY_YFLAME_FLAG_ICICLE 0x2u

struct yetty_yflame_render_config {
    /* Figure placement + width, in pixels. The HEIGHT is derived from the
     * deepest stack (`(max_depth + 1) * frame_height`); bounds_h is ignored. */
    float bounds_x;
    float bounds_y;
    float bounds_w;
    /* Height of one stack level, in pixels. 0 → default (18). */
    float frame_height;
    /* Boxes narrower than this (pixels) are skipped — keeps deep, wide
     * graphs from emitting millions of sub-pixel rectangles. 0 → default. */
    float min_width;
    uint32_t flags; /* YETTY_YFLAME_FLAG_* (default = LABELS) */
};

/*
 * Parse folded-stack `input` and render a flame graph into a fresh ydraw
 * drawable list (caller owns the returned list; destroy with
 * yetty_ydraw_drawable_list_destroy). `config` may be NULL for defaults.
 */
struct yetty_ydraw_drawable_list_result yetty_yflame_render(
    const char *input, size_t len, const struct yetty_yflame_render_config *config);

/*
 * Serialize a rendered drawable list into a YDRAW_BIN OSC envelope and write
 * it to `out`. Returns the number of bytes written.
 */
struct yetty_ycore_size_result yetty_yflame_osc_bin_emit(
    const struct yetty_ydraw_drawable_list *buffer, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFLAME_YFLAME_H */
