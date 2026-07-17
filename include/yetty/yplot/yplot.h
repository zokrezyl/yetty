#ifndef YETTY_YPLOT_YPLOT_H
#define YETTY_YPLOT_YPLOT_H

/*
 * yplot — high-level API for producing yplot composites. Two
 * curve kinds coexist in one instance:
 *
 *   expressions: multi-function plot syntax compiled to yfsvm bytecode and
 *                evaluated per pixel on the GPU
 *      (e.g. "f=sin(x); g=cos(x); @f.color=#FF6B6B")
 *   data buffers: precomputed f32 sample arrays (e.g. live audio)
 *                rendered as anti-aliased line plots from the storage buffer
 *
 * Pipeline:
 *   yexpr_parse_plot(source)   — multi-function plot syntax
 *   yfsvm_compile_multi(ast)   — bytecode for the GPU interpreter
 *   yplot_serialize(uniforms, bytecode + data buffers) — wire bytes
 *   ydraw_core_drawable_list_add_prim(buffer) — attach to a ydraw draw list
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yplot/yplot-gen.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YETTY_YPLOT_FLAG_GRID 0x1u
#define YETTY_YPLOT_FLAG_AXES 0x2u
#define YETTY_YPLOT_FLAG_LABELS 0x4u
/* Set by the sender when the compiled bytecode references `t`/`time`
 * (yfsvm program's `uses_time` is true). The receiving factory
 * subscribes the instance to the shared animation timer when this
 * bit is set; otherwise the plot is static and no timer ticks. */
#define YETTY_YPLOT_FLAG_USES_TIME 0x8u
/* Set by the sender when the compiled bytecode references `y` (yfsvm
 * program's `uses_y` is true): the function is a 2D field f(x,y), and the
 * shader renders it as a colormapped heatmap instead of a line curve. */
#define YETTY_YPLOT_FLAG_FIELD 0x10u
/* Logarithmic (base-10) axis scales. The corresponding range must be
 * strictly positive — the render entry points reject a log axis whose
 * min/max is <= 0. Grid lines and tick labels land on decades. */
#define YETTY_YPLOT_FLAG_XLOG 0x20u
#define YETTY_YPLOT_FLAG_YLOG 0x40u

/* Legend visibility for yetty_yplot_render_config. */
enum yetty_yplot_legend_mode {
    YETTY_YPLOT_LEGEND_AUTO = 0, /* shown when >= 2 named curves */
    YETTY_YPLOT_LEGEND_ON = 1,   /* shown when >= 1 named curve */
    YETTY_YPLOT_LEGEND_OFF = 2,  /* never shown */
};

/* Colormap for field/heatmap plots (matches the shader's colormap_id). */
enum yetty_yplot_colormap {
    YETTY_YPLOT_COLORMAP_VIRIDIS = 0,
    YETTY_YPLOT_COLORMAP_PLASMA = 1,
    YETTY_YPLOT_COLORMAP_MAGMA = 2,
    YETTY_YPLOT_COLORMAP_INFERNO = 3,
};

/* Geometry + axis configuration. NULL fields fall back to defaults. */
struct yetty_yplot_render_config {
    float bounds_x; /* 0   — overridden by canvas at render time */
    float bounds_y; /* 0   — overridden by canvas at render time */
    float bounds_w; /* 400 — width in pixels */
    float bounds_h; /* 200 — height in pixels */
    float x_min;    /* -3.14159 */
    float x_max;    /*  3.14159 */
    float y_min;    /* -1.5 */
    float y_max;    /*  1.5 */
    uint32_t flags; /* YETTY_YPLOT_FLAG_* (default = grid|axes|labels) */

    /* Optional figure text, rendered client-side as MSDF text prims in
     * margins reserved around the plot rect. NULL/empty = absent. The
     * strings only need to live until the render call returns. */
    const char *title;   /* centered above the plot */
    const char *x_label; /* centered below the x tick labels */
    const char *y_label; /* above the y tick column (glyph expansion has
                          * no rotation support, so no rotated label) */

    enum yetty_yplot_legend_mode legend_mode;

    /* Field/heatmap plots: colormap and the value range it spans. Equal
     * min/max means unset — the shader falls back to [-1, 1]. A colorbar
     * with min/mid/max labels is drawn beside field plots whenever axis
     * labels are enabled. */
    enum yetty_yplot_colormap colormap;
    float field_min;
    float field_max;
};

/* Uncertainty display style for a curve's envelope. */
enum yetty_yplot_band_style {
    YETTY_YPLOT_BAND_FILL = 0,     /* translucent fill between lo and hi */
    YETTY_YPLOT_BAND_WHISKERS = 1, /* error bars at decimated samples */
};

/* One data buffer + optional color. color = 0 picks a palette default. */
struct yetty_yplot_buffer_input {
    const float *samples;
    size_t count;
    uint32_t color;   /* ARGB; 0 → palette default for the corresponding slot */
    const char *name; /* optional curve name for the legend; NULL = unnamed */

    /* Uncertainty envelope: `has_band` set means band_lo/band_hi index
     * into the SAME buffer array, pointing at the lower/upper curves. The
     * referenced buffers are usually marked `hidden` so only the envelope
     * shows, not their own line curves. Zero-initialized = no band. */
    bool has_band;
    int band_lo;
    int band_hi;
    enum yetty_yplot_band_style band_style;
    bool hidden; /* band input only: no line curve, no legend entry */

    /* Ring-buffer streaming: the shader unwraps display order from the
     * head (oldest sample) so the newest is at the right edge; producers
     * advance the head with the CMD_UPDATE ring-head op. */
    bool ring;
};

/* Samples loaded from a data file — see yetty_yplot_load_samples(). */
struct yetty_yplot_loaded_samples {
    float *samples; /* heap; caller releases with free() */
    size_t count;
};
YETTY_YRESULT_DECLARE(yetty_yplot_loaded_samples, struct yetty_yplot_loaded_samples);

/* Load plot samples from a data file. Formats: NumPy .npy (little-endian
 * f4/f8/i4/i8, 1-D, C order) and delimited text (CSV/TSV/whitespace).
 * `spec` is "path", "path:column-name" or "path:column-index"; text files
 * default to the second column when several are present (first otherwise),
 * a non-numeric first row is consumed as the header, and '#' lines are
 * skipped. */
struct yetty_yplot_loaded_samples_result yetty_yplot_load_samples(const char *spec);

/* Render `source` (multi-plot-expression syntax — see yexpr_parse_plot)
 * into a fresh ydraw-core buffer holding ONE yplot complex prim.
 *
 * Per-plot color overrides come from `@<name>.color = #RRGGBB` attrs in
 * the source; plots without explicit colors fall back to a built-in
 * 8-slot palette (matches the yaml factory).
 *
 * Caller frees the returned buffer with yetty_ydraw_drawable_list_destroy. */
struct yetty_ydraw_drawable_list_result yetty_yplot_render(
    const char *source, size_t len, const struct yetty_yplot_render_config *config);

/* Same as yetty_yplot_render but also attaches `buffer_count` data buffers
 * (variable count, can be 0). Each data buffer is rendered as an
 * anti-aliased line plot mapped over x_min..x_max; its color comes from
 * the buffer_input entry (0 → palette default).
 *
 * Color slot assignment in the 8-slot palette:
 *   slot 0..M-1            expression curves (M = number of named plots)
 *   slot M..M+N-1 (mod 8)  data buffers in invocation order
 *
 * Caller frees the returned buffer with yetty_ydraw_drawable_list_destroy. */
struct yetty_ydraw_drawable_list_result yetty_yplot_render_with_buffers(
    const char *source, size_t len, const struct yetty_yplot_buffer_input *buffers,
    size_t buffer_count, const struct yetty_yplot_render_config *config);

/* Render a PRECOMPILED yfsvm program (the serialized word array produced by
 * yetty_yfsvm_program_serialize — e.g. emitted by an external frontend such
 * as the Python-subset compiler) into a fresh ydraw-core buffer holding one
 * yplot prim. No expression parsing or compilation happens here.
 *
 * `program` / `program_words` is the serialized program:
 *   [magic][version][func_count][const_count][func_table...][constants...][code...]
 * function_count is read from the header; the USES_TIME / FIELD flags are
 * derived by scanning the code for LOAD_T / LOAD_Y, matching the expression
 * path. Default palette colors are used (no per-curve override channel).
 *
 * Caller frees the returned buffer with yetty_ydraw_drawable_list_destroy. */
struct yetty_ydraw_drawable_list_result yetty_yplot_render_program(
    const uint32_t *program, uint32_t program_words,
    const struct yetty_yplot_render_config *config);

/* DCS envelope (YETTY_DCS_YDRAW_BIN, same wire format as ycat / yecho).
 * Returns bytes written; ERR on failure. */
struct yetty_ycore_size_result yetty_yplot_dcs_bin_emit(
    const struct yetty_ydraw_drawable_list *buffer, FILE *out);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLOT_YPLOT_H */
