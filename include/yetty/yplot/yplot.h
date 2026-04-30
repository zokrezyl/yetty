#ifndef YETTY_YPLOT_YPLOT_H
#define YETTY_YPLOT_YPLOT_H

/*
 * yplot — high-level API for producing a yplot complex primitive from
 * a function-expression string.
 *
 * Pipeline:
 *   yexpr_parse_plot(source)  — multi-function plot syntax with per-plot
 *                               attrs (`f = sin(x); g = cos(x); @f.color=...`)
 *   yfsvm_compile_multi(ast)  — bytecode for the GPU interpreter
 *   yplot_serialize(uniforms, bytecode) — wire bytes
 *   ypaint_core_buffer_add_prim(buffer)  — attach to a ypaint buffer
 *
 * The frontend tool (tools/yplot) wraps this with a CLI; yecho's
 * `{plot: ...}` block uses the same path internally.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <yetty/ycore/result.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/yplot/yplot-gen.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YETTY_YPLOT_FLAG_GRID   0x1u
#define YETTY_YPLOT_FLAG_AXES   0x2u
#define YETTY_YPLOT_FLAG_LABELS 0x4u

/* Geometry + axis configuration. NULL fields fall back to defaults. */
struct yetty_yplot_render_config {
    float bounds_x;     /* 0   — overridden by canvas at render time */
    float bounds_y;     /* 0   — overridden by canvas at render time */
    float bounds_w;     /* 400 — width in pixels */
    float bounds_h;     /* 200 — height in pixels */
    float x_min;        /* -3.14159 */
    float x_max;        /*  3.14159 */
    float y_min;        /* -1.5 */
    float y_max;        /*  1.5 */
    uint32_t flags;     /* YETTY_YPLOT_FLAG_* (default = grid|axes|labels) */
};

/* Render `source` (multi-plot-expression syntax — see yexpr_parse_plot)
 * into a fresh ypaint-core buffer holding ONE yplot complex prim.
 *
 * Per-plot color overrides come from `@<name>.color = #RRGGBB` attrs in
 * the source; plots without explicit colors fall back to a built-in
 * 8-slot palette (matches the yaml factory).
 *
 * Caller frees the returned buffer with yetty_ypaint_core_buffer_destroy. */
struct yetty_ypaint_core_buffer_result yetty_yplot_render(
    const char *source, size_t len, const struct yetty_yplot_render_config *config);

/* OSC envelope (YETTY_OSC_YPAINT_BIN, same wire format as ycat / yecho).
 * Returns bytes written; ERR on failure. */
struct yetty_ycore_size_result yetty_yplot_osc_bin_emit(
    const struct yetty_ypaint_core_buffer *buffer, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLOT_YPLOT_H */
