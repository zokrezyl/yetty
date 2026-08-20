#ifndef YETTY_YPLOT_RESOLVE_H
#define YETTY_YPLOT_RESOLVE_H

/*
 * yplot expression resolution — the shared boundary every expression-rendering
 * frontend (the shell `yplot`, yecho plot blocks, yplot-yaml, and the future
 * api_yplot facade) funnels through, so one DSL string resolves identically
 * everywhere.
 *
 * This is IMPLEMENTATION-level infrastructure, not the stable api_yplot ABI:
 * `struct yetty_yplot_resolved` carries API-independent semantic values only —
 * it never exposes GPU uniforms or wire layout. A caller merges its own
 * overrides (presence-aware, so "grid off" differs from "grid unset") with the
 * expression under an explicit precedence policy.
 */

#include <stdbool.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/yexpr/yexpr.h>
#include <yetty/yplot/yplot.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Who wins when the expression and the caller's options both name a property. */
enum yetty_yplot_attr_precedence {
    YETTY_YPLOT_EXPRESSION_WINS, /* shell: the DSL string is authoritative */
    YETTY_YPLOT_CONFIG_WINS,     /* facade: an explicit setter wins over the DSL */
};

/* Presence bits for yetty_yplot_options — a field is meaningful only when its
 * bit is set, so an explicit false/zero is distinguishable from "unset". */
enum yetty_yplot_option_present {
    YETTY_YPLOT_OPT_WIDTH = 1u << 0,
    YETTY_YPLOT_OPT_HEIGHT = 1u << 1,
    YETTY_YPLOT_OPT_X_RANGE = 1u << 2,
    YETTY_YPLOT_OPT_Y_RANGE = 1u << 3,
    YETTY_YPLOT_OPT_TITLE = 1u << 4,
    YETTY_YPLOT_OPT_X_LABEL = 1u << 5,
    YETTY_YPLOT_OPT_Y_LABEL = 1u << 6,
    YETTY_YPLOT_OPT_GRID = 1u << 7,
    YETTY_YPLOT_OPT_AXES = 1u << 8,
    YETTY_YPLOT_OPT_LABELS = 1u << 9,
    YETTY_YPLOT_OPT_X_LOG = 1u << 10,
    YETTY_YPLOT_OPT_Y_LOG = 1u << 11,
    YETTY_YPLOT_OPT_LEGEND = 1u << 12,
    YETTY_YPLOT_OPT_COLORMAP = 1u << 13,
    YETTY_YPLOT_OPT_FIELD = 1u << 14,
};

/* Presence-aware caller overrides. String pointers are borrowed and must
 * outlive the resolve+emit that consumes them. */
struct yetty_yplot_options {
    uint32_t present; /* bitmask of yetty_yplot_option_present */
    float width, height;
    float x_min, x_max, y_min, y_max;
    const char *title, *x_label, *y_label;
    bool grid, axes, labels, x_log, y_log;
    enum yetty_yplot_legend_mode legend;
    enum yetty_yplot_colormap colormap;
    float field_min, field_max;
};

/* Resolved figure/axis options — the expression merged with overrides.
 * Semantic only (no uniforms/wire layout). `has_*` false means "let the caller
 * or canvas apply its default". Per-curve style (colors) is NOT here: it lives
 * in the step-3 render plan alongside the compiled program and data buffers.
 *
 * Title/label pointers BORROW into the parsed expression or the caller's
 * options — those must outlive not just the resolve() call but every use of
 * this resolved object (e.g. the subsequent emit). */
struct yetty_yplot_resolved {
    bool has_width, has_height;
    float width, height;
    bool has_x_range, has_y_range;
    float x_min, x_max, y_min, y_max;
    bool grid, axes, labels, x_log, y_log;
    const char *title, *x_label, *y_label;
    enum yetty_yplot_legend_mode legend;
    enum yetty_yplot_colormap colormap;
    bool has_field;
    float field_min, field_max;
};

/* Resolve a parsed plot expression against caller overrides under `precedence`.
 * Validates BOTH sources: the whole parsed expression (unknown reserved/curve/
 * buffer targets, unknown attributes, bad enum keywords, invalid curve color;
 * order-independent) and the caller `options` (finite/positive dimensions,
 * ordered finite ranges, in-range enums, no unknown presence bits). `options`
 * may be NULL. `*out` is written only on success, so a failed resolve never
 * leaves partially-resolved state. Returns an error Result on any violation. */
struct yetty_ycore_void_result yetty_yplot_resolve(const struct yetty_yexpr_plot_expr *expression,
                                                   const struct yetty_yplot_options *options,
                                                   enum yetty_yplot_attr_precedence precedence,
                                                   struct yetty_yplot_resolved *out);

/* A complete, self-contained internal render plan — the single input to the
 * canonical emission path. It bundles the built render state (uniforms, with
 * USES_TIME/FIELD already derived from the compiled program), the compiled
 * program + data buffers, the legend (name/color, borrowed), and the chrome
 * strings (borrowed). Impl-level, NOT the api_yplot ABI. Bytecode/data/legend/
 * chrome pointers must outlive the emit. `uniforms.bounds_x/y` are ignored —
 * emit_into positions the figure at the origin it is given. */
struct yetty_yplot_render_plan {
    struct yetty_yplot_uniforms uniforms;
    struct yetty_yplot_buffers buffers;
    const char *legend_names[YETTY_YEXPR_MAX_PLOT_DEFS + 8];
    uint32_t legend_colors[YETTY_YEXPR_MAX_PLOT_DEFS + 8];
    uint32_t legend_count;
    const char *title, *x_label, *y_label; /* borrowed; NULL/empty = absent */
    enum yetty_yplot_legend_mode legend_mode;
};

/* Emit a render plan (plot prim + chrome: axis labels, title, legend, colorbar)
 * INTO an existing drawable list, with the figure positioned at (origin_x,
 * origin_y). The caller owns `dest` and its scene bounds. This is the one
 * emission path shared by the shell render, yecho, and yplot-yaml. */
struct yetty_ycore_void_result yetty_yplot_emit_into(const struct yetty_yplot_render_plan *plan,
                                                     struct yetty_ydraw_drawable_list *dest,
                                                     float origin_x, float origin_y);

/* The one high-level shared path: parse a plot expression, resolve it against
 * `config` (expression attributes win), compile + build the render plan, and
 * emit it (curves + chrome) INTO `dest` at (origin_x, origin_y). The shell
 * render, yecho, and yplot-yaml all call this — none reproduce uniform
 * construction or chrome layout. `api_buffers` are extra data buffers appended
 * after source-declared ones (may be NULL). If non-NULL, `*out_figure_w` /
 * `*out_figure_h` receive the emitted figure size so the caller can set scene
 * bounds or advance a layout cursor. The caller owns `dest` and its scene
 * bounds. */
struct yetty_ycore_void_result yetty_yplot_emit_expression(
    const char *source, size_t source_len, const struct yetty_yplot_buffer_input *api_buffers,
    size_t api_buffer_count, const struct yetty_yplot_render_config *config,
    struct yetty_ydraw_drawable_list *dest, float origin_x, float origin_y, float *out_figure_w,
    float *out_figure_h);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLOT_RESOLVE_H */
