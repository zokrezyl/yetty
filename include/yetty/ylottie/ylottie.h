#ifndef YETTY_YLOTTIE_YLOTTIE_H
#define YETTY_YLOTTIE_YLOTTIE_H

/*
 * ylottie - render one frame of a Lottie (Bodymovin) animation into a ydraw
 * buffer.
 *
 * Scope: the static-graphics subset of the Lottie schema that maps cleanly
 * onto ydraw SDF primitives and MSDF text spans, evaluated at a single point
 * in time. Lottie is an animation format; this engine flattens the document
 * AT A CHOSEN FRAME into a static ydraw buffer — the same shape as ysvg /
 * ymarkdown / ypdf, where the caller owns one buffer. Real-time playback
 * (a timeline-driven figure) is out of scope here.
 *
 * Pipeline (mirrors ysvg):
 *   1. JSON parse: source bytes → DOM tree (ylottie-json.c).
 *   2. Frame select: default = composition in-point `ip`; override with
 *      --frame / --time.
 *   3. Layer walk (ylottie-paint.c): for each shape layer, evaluate its
 *      transform (and parent chain) at the frame, walk the shape groups,
 *      and emit ydraw SDF primitives:
 *        ellipse   → SDF circle / ellipse (or flattened polyline when rotated)
 *        rect      → SDF box / rounded_box (or 4 segments when rotated)
 *        path (sh) → cubic bezier flattened to polyline segments
 *        polystar  → analytic vertices → closed polyline segments
 *      Fills approximate a solid interior by tracing the perimeter (the SDF
 *      set has no arbitrary-polygon fill); strokes map straight to SDF
 *      stroke width.
 *   4. Text layers (ty 5) emit MSDF TEXT_DRAWABLE_LIST drawable-list entries via
 *      yetty_ydraw_drawable_list_add_text.
 *
 * The composition `w`/`h` determine the user-space bounds; a width-fit policy
 * (same as ysvg) maps them to display pixels.
 *
 * The output buffer is owned by the caller (free with
 * yetty_ydraw_drawable_list_destroy).
 *
 * Not covered: image/precomp layers, masks, track mattes, effects, gradients
 * (approximated by their first stop colour), trim paths, blend modes,
 * merge/repeater/rounded-corner modifiers.
 */

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ydraw-list/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ylottie_render_config {
    /* Display cell size in pixels — used to derive a default text font size
     * when a text layer carries none. */
    uint32_t cell_width;
    uint32_t cell_height;
    /* Surface dimensions in cells — used as the scene-bounds fallback when
     * the document carries neither w/h nor anything usable. */
    uint32_t width_cells;
    uint32_t height_cells;
};

struct yetty_ylottie_render_output {
    struct yetty_ydraw_drawable_list *buffer;
    float scene_width;
    float scene_height;
};

YETTY_YRESULT_DECLARE(yetty_ylottie_render, struct yetty_ylottie_render_output);

/* Render one frame of a Lottie document into a fresh ydraw buffer. `content`
 * need not be NUL terminated; `content_len` is authoritative. `args` is a
 * flag string of the same shape ysvg/ymarkdown use:
 *   --frame=<float>          render this frame number (default = ip)
 *   --time=<seconds>         render at this time (frame = time * fr); wins
 *                            over --frame when both are given
 *   --bg=<#RRGGBB|#RRGGBBAA> background fill (default transparent)
 * Pass NULL/0 for defaults. */
struct yetty_ylottie_render_result yetty_ylottie_render(
    const char *content, size_t content_len, const char *args, size_t args_len,
    const struct yetty_ylottie_render_config *config);

/* Cheap content sniff used by ycat's type detector: does this byte buffer
 * look like a Lottie/Bodymovin JSON document? (A leading '{' plus the
 * tell-tale "layers" array and an animation marker such as "fr"/"ip"/"op".)
 * Returns 1 if it looks like Lottie, 0 otherwise. Does not fully parse. */
int yetty_ylottie_can_parse(const char *content, size_t len);

/* Composition header — the timeline metadata a player needs to drive a
 * full-animation render (one frame at a time over [in_point, out_point)). */
struct yetty_ylottie_info {
    float frame_rate; /* `fr` — frames per second */
    float in_point;   /* `ip` — first frame */
    float out_point;  /* `op` — one past the last frame */
    float width;      /* `w`  — composition width in user units */
    float height;     /* `h`  — composition height in user units */
    int layer_count;  /* number of entries in `layers` */
};

YETTY_YRESULT_DECLARE(yetty_ylottie_info, struct yetty_ylottie_info);

/* Parse just enough to return the composition header. Cheaper to call once
 * up front than to re-derive per frame. */
struct yetty_ylottie_info_result yetty_ylottie_inspect(const char *content, size_t len);

/* Parse a #RGB / #RRGGBB / #RRGGBBAA colour into the ABGR word ydraw uses.
 * Returns 1 on success, 0 on a malformed string. */
int yetty_ylottie_parse_color(const char *s, size_t len, uint32_t *out_abgr);

/*=============================================================================
 * Animation handle — parse ONCE, render MANY frames.
 *
 * A player (or any caller rendering more than one frame) should create an
 * animation, then call render_frame per frame. The JSON is parsed a single
 * time at create; each render_frame only allocates a fresh ydraw buffer and
 * walks the already-parsed DOM. `yetty_ylottie_render` below is the one-shot
 * convenience wrapper built on top of this for single-frame callers (ycat).
 *===========================================================================*/

struct yetty_ylottie_animation; /* opaque; owns the parsed document */

YETTY_YRESULT_DECLARE(yetty_ylottie_animation_ptr, struct yetty_ylottie_animation *);

/* Parse `content` once and precompute scene bounds from `config`. */
struct yetty_ylottie_animation_ptr_result yetty_ylottie_animation_create(
    const char *content, size_t content_len, const struct yetty_ylottie_render_config *config);

/* Composition header, available without re-parsing. */
struct yetty_ylottie_info yetty_ylottie_animation_info(const struct yetty_ylottie_animation *anim);

/* Render one frame into a fresh ydraw buffer (caller owns it; free with
 * yetty_ydraw_drawable_list_destroy). `bg_abgr` is the background fill in ydraw's
 * ABGR layout, or 0 for a transparent background. The returned buffer is fully
 * self-contained — it does NOT reference the animation, so the animation may
 * be destroyed while emitted buffers are still in flight. */
struct yetty_ylottie_render_result yetty_ylottie_animation_render_frame(
    const struct yetty_ylottie_animation *anim, float frame, uint32_t bg_abgr);

void yetty_ylottie_animation_destroy(struct yetty_ylottie_animation *anim);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YLOTTIE_YLOTTIE_H */
