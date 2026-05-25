#ifndef YETTY_YGUI_OLD_YGUI_YPLOT_H
#define YETTY_YGUI_OLD_YGUI_YPLOT_H

/*
 * ygui_yplot — yplot widget for ygui.
 *
 * Wraps a yplot complex prim in a RICH-style widget. Two content modes
 * are supported and can be combined in a single instance:
 *
 *   - expression source ("f=sin(x+t); g=cos(x+t); @f.color=#ff6b6b")
 *     compiled to yfsvm bytecode and evaluated per pixel on the GPU.
 *
 *   - precomputed data buffers (f32 sample arrays, e.g. live audio)
 *     drawn as anti-aliased line plots over x_min..x_max.
 *
 * Updating content via the set_* helpers builds a fresh prim and hands
 * it to the widget's underlying RICH surface. The widget id stays
 * stable so the receiving scene-canvas treats the swap as an in-place
 * update rather than a destroy+create. Useful for live audio scopes,
 * scrubbed expression playgrounds, etc.
 *
 * Bounds note: when `config->bounds_w` / `config->bounds_h` are 0,
 * they default to the widget's currently-resolved content box (or to
 * yplot's 400×200 default before the first layout pass). Pass non-zero
 * values to lock the prim's painted size against the widget's size.
 */

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/yplot/yplot.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ygui_old_engine;
struct yetty_ygui_old_widget;

/* Build a yplot widget from an expression source. `source` is borrowed
 * — only read during the call, not retained. `source_len == 0` with
 * a non-NULL `source` is treated as "auto-strlen" (standard C
 * convention). `config` may be NULL to use yplot's defaults. NULL is
 * returned on parse / serialize failure or OOM. */
struct yetty_ygui_old_widget *yetty_ygui_old_engine_yplot_from_source(
    struct yetty_ygui_old_engine *engine, const char *id, float x, float y, float w, float h,
    const char *source, size_t source_len, const struct yetty_yplot_render_config *config);

/* Same as above with N attached data buffers. Pass `source=NULL`/
 * `source_len=0` for buffer-only (pure data plot, no expressions);
 * pass `buffers=NULL`/`buffer_count=0` for source-only. `buffers` and
 * the f32 sample arrays they point at are borrowed — yplot copies the
 * samples into the prim, so the arrays may be freed / reused after
 * this call returns. */
struct yetty_ygui_old_widget *yetty_ygui_old_engine_yplot_from_buffers(
    struct yetty_ygui_old_engine *engine, const char *id, float x, float y, float w, float h,
    const char *source, size_t source_len, const struct yetty_yplot_buffer_input *buffers,
    size_t buffer_count, const struct yetty_yplot_render_config *config);

/* Replace the widget's content with a fresh prim built from `source`.
 * The widget id is preserved so the receiving canvas applies the
 * change as an in-place update. */
struct yetty_ycore_void_result yetty_ygui_old_widget_yplot_set_source(
    struct yetty_ygui_old_widget *widget, const char *source, size_t source_len,
    const struct yetty_yplot_render_config *config);

/* set_source plus N data buffers — primary entry point for live
 * updates (call once per frame with the freshest samples). */
struct yetty_ycore_void_result yetty_ygui_old_widget_yplot_set_buffers(
    struct yetty_ygui_old_widget *widget, const char *source, size_t source_len,
    const struct yetty_yplot_buffer_input *buffers, size_t buffer_count,
    const struct yetty_yplot_render_config *config);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_OLD_YGUI_YPLOT_H */
