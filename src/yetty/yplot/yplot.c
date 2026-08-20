/*
 * yplot.c — high-level convenience wrappers around the auto-generated
 * yplot-gen.c API. See include/yetty/yplot/yplot.h for the contract.
 */

#include <yetty/yplot/yplot.h>

#include <yetty/yexpr/yexpr.h>
#include <yetty/yfsvm/compiler.h>
#include <yetty/yplot/resolve.h>
#include <yetty/yface/yface.h>
#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/ycore/types.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/yterminal/dcs-codes.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Default 8-color palette — matches the yaml factory and the demo plots. */
static const uint32_t YPLOT_PALETTE[8] = {
    0xFFFF6B6B, 0xFF4ECDC4, 0xFFFFE66D, 0xFF95E1D3, 0xFFF38181, 0xFFAA96DA, 0xFF72D6C9, 0xFFFCBF49,
};

static int parse_hex_color(const char *s, uint32_t *out)
{
    if (!s || s[0] != '#') {
        return 0;
    }
    const char *h = s + 1;
    size_t hl = strlen(h);
    char buf[7];
    if (hl == 3) {
        buf[0] = h[0];
        buf[1] = h[0];
        buf[2] = h[1];
        buf[3] = h[1];
        buf[4] = h[2];
        buf[5] = h[2];
        buf[6] = '\0';
    } else if (hl == 6) {
        memcpy(buf, h, 6);
        buf[6] = '\0';
    } else {
        return 0;
    }
    char *endp = NULL;
    unsigned long v = strtoul(buf, &endp, 16);
    if (!endp || *endp != '\0') {
        return 0;
    }
    uint32_t r = (uint32_t)((v >> 16) & 0xFF);
    uint32_t g = (uint32_t)((v >> 8) & 0xFF);
    uint32_t b = (uint32_t)(v & 0xFF);
    /* Pack 0xAARRGGBB to match the palette table and the plot shader's
     * yplot_unpack_color (which reads R at bits 16-23). */
    *out = 0xFF000000u | (r << 16) | (g << 8) | b;
    return 1;
}

/* Fill `u` with caller config geometry/ranges/flags and palette-default
 * colors. Shared by the expression path and the precompiled-program path. */
static void yplot_init_base_uniforms(const struct yetty_yplot_render_config *config,
                                     struct yetty_yplot_uniforms *u)
{
    memset(u, 0, sizeof(*u));
    u->bounds_x = config ? config->bounds_x : 0.0f;
    u->bounds_y = config ? config->bounds_y : 0.0f;
    u->bounds_w = (config && config->bounds_w > 0.0f) ? config->bounds_w : 400.0f;
    u->bounds_h = (config && config->bounds_h > 0.0f) ? config->bounds_h : 200.0f;
    if (config && (config->x_min != 0.0f || config->x_max != 0.0f)) {
        u->x_min = config->x_min;
        u->x_max = config->x_max;
    } else {
        u->x_min = -3.14159f;
        u->x_max = 3.14159f;
    }
    if (config && (config->y_min != 0.0f || config->y_max != 0.0f)) {
        u->y_min = config->y_min;
        u->y_max = config->y_max;
    } else {
        u->y_min = -1.5f;
        u->y_max = 1.5f;
    }
    /* USES_TIME / FIELD are DERIVED from the compiled program (LOAD_T / LOAD_Y),
     * never honored from caller flags — so a static plot cannot be forced to
     * animate and a 1-D function cannot be forced into heatmap rendering. The
     * compile/scan paths OR these bits in afterward. */
    uint32_t caller_flags =
        (config && config->flags)
            ? config->flags
            : (YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS);
    u->flags = caller_flags & ~(uint32_t)(YETTY_YPLOT_FLAG_USES_TIME | YETTY_YPLOT_FLAG_FIELD);

    u->colormap_id = config ? (uint32_t)config->colormap : 0u;
    if (config && config->field_min != config->field_max) {
        u->field_min = config->field_min;
        u->field_max = config->field_max;
    } else {
        u->field_min = -1.0f;
        u->field_max = 1.0f;
    }

    for (int i = 0; i < 8; i++) {
        u->colors[i] = YPLOT_PALETTE[i];
    }
}

/* C twin of the shader's yplot_colormap (Matt Zucker's 6th-order fits of
 * the matplotlib maps) — paints the client-side colorbar. Returns the
 * SDF-box color packing (0xAABBGGRR). Keep coefficients in sync with
 * yplot.wgsl. */
static uint32_t yplot_colormap_sample(uint32_t colormap_id, float t_in)
{
    static const double fits[4][7][3] = {
        {/* viridis */
         {0.2777273272234177, 0.005407344544966578, 0.3340998053353061},
         {0.1050930431085774, 1.404613529898575, 1.384590162594685},
         {-0.3308618287255563, 0.214847559468213, 0.09509516302823659},
         {-4.634230498983486, -5.799100973351585, -19.33244095627987},
         {6.228269936347081, 14.17993336680509, 56.69055260068105},
         {4.776384997670288, -13.74514537774601, -65.35303263337234},
         {-5.435455855934631, 4.645852612178535, 26.3124352495832}},
        {/* plasma */
         {0.05873234392399702, 0.02333670892565664, 0.5433401826748754},
         {2.176514634195958, 0.2383834171260182, 0.7539604599784036},
         {-2.689460476458034, -7.455851135738909, 3.110799939717086},
         {6.130348345893603, 42.3461881477227, -28.51885465332158},
         {-11.10743619062271, -82.66631109428045, 60.13984767418263},
         {10.02306557647065, 71.41361770095349, -54.07218655560067},
         {-3.658713842777788, -22.93153465461149, 18.19190778539828}},
        {/* magma */
         {-0.002136485053939582, -0.000749655052795221, -0.005386127855323933},
         {0.2516605407371642, 0.6775232436837668, 2.494026599312351},
         {8.353717279216625, -3.577719514958484, 0.3144679030132573},
         {-27.66873308576866, 14.26473078096533, -13.64921318813922},
         {52.17613981234068, -27.94360607168351, 12.94416944238394},
         {-50.76852536473588, 29.04658282127291, 4.23415299384598},
         {18.65570506591883, -11.48977351997711, -5.601961508734096}},
        {/* inferno */
         {0.0002189403691192265, 0.001651004631001012, -0.01948089843709184},
         {0.1065134194856116, 0.5639564367884091, 3.932712388889277},
         {11.60249308247187, -3.972853965665698, -15.9423941062914},
         {-41.70399613139459, 17.43639888205313, 44.35414519872813},
         {77.162935699427, -33.40235894210092, -81.80730925738993},
         {-71.31942824499214, 32.62606426397723, 73.20951985803202},
         {25.13112622477341, -12.24266895238567, -23.07032500287172}},
    };
    const double (*coeff)[3] = fits[colormap_id < 4u ? colormap_id : 0u];
    double t = t_in < 0.0f ? 0.0 : (t_in > 1.0f ? 1.0 : (double)t_in);
    uint32_t packed = 0xFF000000u;
    for (int channel = 0; channel < 3; channel++) {
        double value = coeff[6][channel];
        for (int order = 5; order >= 0; order--) {
            value = value * t + coeff[order][channel];
        }
        if (value < 0.0) {
            value = 0.0;
        } else if (value > 1.0) {
            value = 1.0;
        }
        /* SDF boxes read R at bits 0-7, G at 8-15, B at 16-23. */
        packed |= (uint32_t)(value * 255.0 + 0.5) << (channel * 8);
    }
    return packed;
}

/* Fill `u` with caller config values, palette-default colors, and a
 * compiled bytecode block from `source` (may be empty when caller only
 * wants buffer curves). `bc_buf` / `bc_cap` is a caller-owned scratch
 * area for the serialized yfsvm program. The parsed plot expression is
 * also handed back to the caller in `*out_plot` so it can synthesise
 * data buffers from `name=buffer` declarations; `expr_arena` is the
 * caller-owned node arena the parsed AST points into. */
static struct yetty_ycore_void_result yplot_build_uniforms_and_bytecode(
    const char *source, size_t source_len, const struct yetty_yplot_render_config *config,
    uint32_t *bc_buf, uint32_t bc_cap, struct yetty_yplot_uniforms *u, uint32_t *out_bc_len,
    struct yetty_yexpr_arena *expr_arena, struct yetty_yexpr_plot_expr *out_plot)
{
    yplot_init_base_uniforms(config, u);

    *out_bc_len = 0;
    if (out_plot) {
        memset(out_plot, 0, sizeof(*out_plot));
    }
    if (!source || source_len == 0) {
        u->function_count = 0;
        return YETTY_OK_VOID();
    }

    /* Parse expression(s) using yexpr's plot-syntax. */
    struct yetty_yexpr_plot_expr_result pr = yetty_yexpr_parse_plot(source, source_len, expr_arena);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "yplot: expression parse failed");
    if (out_plot) {
        *out_plot = pr.value;
    }

    /* Domain / viewport ranges (`x=A..B`, `@view=…`) are applied later from the
     * resolved options (yplot_apply_resolved), the single source of truth — not
     * duplicated here. */
    u->function_count = pr.value.def_count;
    if (u->function_count > 8) {
        u->function_count = 8;
    }

    /* Per-plot @<name>.color overrides. */
    for (uint32_t i = 0; i < pr.value.attr_count; i++) {
        const struct yetty_yexpr_plot_attr *attr = &pr.value.attrs[i];
        if (strcmp(attr->attr_name, "color") != 0) {
            continue;
        }
        for (uint32_t j = 0; j < u->function_count; j++) {
            if (strcmp(pr.value.defs[j].name, attr->plot_name) == 0) {
                uint32_t c;
                if (parse_hex_color(attr->value, &c)) {
                    u->colors[j] = c;
                }
                break;
            }
        }
    }

    /* A plot may declare only data buffers and no functions — e.g. an inline
     * `data=buffer; @data.values=…` meant to render directly as a curve.
     * Compiling a zero-function program is an error, so skip the compile: emit
     * no bytecode and let the data buffers (if any) render on their own. */
    if (pr.value.def_count == 0) {
        *out_bc_len = 0;
        return YETTY_OK_VOID();
    }

    /* Compile to bytecode. The compile_multi entry threads the plot
     * expression's buffer table into the codegen so f(x) calls resolve
     * to LOAD_S against the buffer's slot. */
    struct yetty_yfsvm_program_result prog = yetty_yfsvm_compile_multi(&pr.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, prog, "yplot: yfsvm compile failed");

    /* Tell the receiver to subscribe to the animation timer when the
     * compiled program references LOAD_T. The hook reads this bit at
     * instance_create — saves scanning the bytecode every time. */
    if (prog.value.uses_time) {
        u->flags |= YETTY_YPLOT_FLAG_USES_TIME;
    }

    /* A program that reads `y` is a 2D field f(x,y) — flag it so the shader
     * renders a colormapped heatmap rather than treating the result as a
     * line height. */
    if (prog.value.uses_y) {
        u->flags |= YETTY_YPLOT_FLAG_FIELD;
    }

    uint32_t bc_len = yetty_yfsvm_program_serialize(&prog.value, bc_buf, bc_cap);
    if (bc_len == 0) {
        return YETTY_ERR(yetty_ycore_void, "yplot: bytecode serialize failed");
    }
    *out_bc_len = bc_len;
    return YETTY_OK_VOID();
}

/* Upper bound on labelled ticks per axis. Sized for a dense linear axis
 * (8 divisions) and a mantissa-annotated log axis (3 decades × {1,2,5}). */
#define YPLOT_MAX_TICKS 24

/* The tick values chosen for one axis. Filled linearly (nice steps) or
 * logarithmically (decades, with {2,5} mantissas on short ranges); the
 * label emitters and margin planner iterate the same array either way. */
struct yplot_axis_ticks {
    double values[YPLOT_MAX_TICKS];
    uint32_t count;
};

/* Axis-label layout: reserved margins + chosen ticks for one figure.
 * The single-fragment plot shader cannot render text, so tick numbers,
 * the title and the axis-name labels are added as separate TEXT prims
 * (default MSDF font) into the same drawable list, laid out in margins
 * reserved by insetting the plot rect. */
struct yplot_label_plan {
    bool enabled;
    float font_size;
    float gap;           /* padding between labels and the plot edge */
    float left_margin;   /* reserved on the left for y-axis labels */
    float bottom_margin; /* reserved on the bottom for x tick labels */
    double x_step;       /* nice tick spacing along x (0 on a log axis) */
    double y_step;       /* nice tick spacing along y (0 on a log axis) */
    bool x_log;          /* base-10 log x axis */
    bool y_log;          /* base-10 log y axis */
    struct yplot_axis_ticks x_ticks;
    struct yplot_axis_ticks y_ticks;
    uint32_t color; /* ARGB packed 0xAABBGGRR */

    /* Figure chrome (title / axis-name strings from the render config). */
    float title_font;
    float top_margin;     /* title strip + y-label strip above the plot */
    float x_label_margin; /* extra strip under the x tick labels */
    bool have_title;
    bool have_x_label;
    bool have_y_label;
};

/* Fraction [0..1] of `value` along an axis range, honouring the axis scale.
 * Log axes map in log10 space; callers guarantee positive ranges (the
 * render entry points reject non-positive log ranges up front). */
static double yplot_axis_fraction(double value, double range_min, double range_max, bool log_scale)
{
    if (log_scale) {
        double log_min = log10(range_min);
        double log_max = log10(range_max);
        return (log10(value) - log_min) / (log_max - log_min);
    }
    return (value - range_min) / (range_max - range_min);
}

/* One legend row: a curve's name and the color it is drawn in. Rendered as a
 * color swatch + name text in a reserved right margin, so which curve is which
 * is readable without inspecting colors by eye. */
struct yplot_legend_entry {
    const char *name;
    uint32_t color;
};

/* Snap a raw tick step to the nearest "nice" value (1/2/5 × 10ⁿ) so tick
 * numbers land on round values instead of arbitrary fractions. */
static double yplot_nice_step(double range, int divisions)
{
    if (!(range > 0.0) || divisions < 1) {
        return 1.0;
    }
    double raw = range / (double)divisions;
    double magnitude = pow(10.0, floor(log10(raw)));
    double normalized = raw / magnitude; /* in [1, 10) */
    double snapped;
    if (normalized < 1.5) {
        snapped = 1.0;
    } else if (normalized < 3.0) {
        snapped = 2.0;
    } else if (normalized < 7.0) {
        snapped = 5.0;
    } else {
        snapped = 10.0;
    }
    return snapped * magnitude;
}

/* Format one tick value compactly. Large magnitudes get k/M suffixes so
 * token-style counts stay short; %g trims float dust and trailing zeros. */
static void yplot_format_tick(double value, double step, char *out, size_t out_size)
{
    /* Snap near-zero values to a clean 0 so "-0" / float dust never prints. */
    if (fabs(value) < step * 1.0e-3) {
        value = 0.0;
    }
    double magnitude = fabs(value);
    if (magnitude >= 1.0e6) {
        snprintf(out, out_size, "%gM", value / 1.0e6);
    } else if (magnitude >= 1.0e3) {
        snprintf(out, out_size, "%gk", value / 1.0e3);
    } else {
        snprintf(out, out_size, "%g", value);
    }
}

/* Rough on-screen width of a label with the default proportional font — the
 * yplot library has no font metrics, so use the same 0.6·font_size·len
 * estimate the ychart fallback uses. */
static float yplot_label_width(const char *text, float font_size)
{
    return font_size * 0.6f * (float)strlen(text);
}

/* Fill `ticks` with multiples of `step` covering [range_min, range_max]. */
static void yplot_linear_ticks(double range_min, double range_max, double step,
                               struct yplot_axis_ticks *ticks)
{
    ticks->count = 0;
    if (!(step > 0.0)) {
        return;
    }
    double first = ceil(range_min / step) * step;
    for (double value = first; value <= range_max + step * 1.0e-6; value += step) {
        if (ticks->count >= YPLOT_MAX_TICKS) {
            break;
        }
        ticks->values[ticks->count++] = value;
    }
}

/* Fill `ticks` for a base-10 log axis over [range_min, range_max] (both
 * strictly positive). Wide ranges get decade ticks (thinned to at most
 * ~8 by striding whole decades); ranges under three decades also get the
 * {2, 5} mantissa ticks; a range inside a single decade falls back to
 * linear nice ticks (still positioned logarithmically by the emitters). */
static void yplot_log_ticks(double range_min, double range_max, struct yplot_axis_ticks *ticks)
{
    /* The range typically arrives through f32 config/uniforms, so a value
     * like 0.1f lands ~1.2e-8 relative away from the exact decade. The
     * tolerance must swallow the f32 representation error or the decade
     * sitting exactly on a range edge is dropped. */
    const double edge_tolerance = 1.0e-6;
    ticks->count = 0;
    int first_exponent = (int)ceil(log10(range_min) - edge_tolerance);
    int last_exponent = (int)floor(log10(range_max) + edge_tolerance);
    int decade_count = last_exponent - first_exponent + 1;

    if (decade_count >= 3) {
        int stride = 1 + (decade_count - 1) / 8;
        for (int exponent = first_exponent; exponent <= last_exponent; exponent += stride) {
            if (ticks->count >= YPLOT_MAX_TICKS) {
                break;
            }
            ticks->values[ticks->count++] = pow(10.0, exponent);
        }
        return;
    }

    if (decade_count >= 1) {
        /* Short log range: decades plus the 2× and 5× mantissas that fall
         * inside the range, in ascending order. */
        static const double mantissas[3] = {1.0, 2.0, 5.0};
        for (int exponent = first_exponent - 1; exponent <= last_exponent; exponent++) {
            double decade = pow(10.0, exponent);
            for (int mi = 0; mi < 3; mi++) {
                double value = decade * mantissas[mi];
                if (value < range_min * (1.0 - edge_tolerance) ||
                    value > range_max * (1.0 + edge_tolerance)) {
                    continue;
                }
                if (ticks->count >= YPLOT_MAX_TICKS) {
                    return;
                }
                ticks->values[ticks->count++] = value;
            }
        }
        if (ticks->count >= 2) {
            return;
        }
    }

    /* Sub-decade range (e.g. 3..7): linear nice ticks read better than a
     * single decade line; positions still map through log10. */
    double step = yplot_nice_step(range_max - range_min, 4);
    yplot_linear_ticks(range_min, range_max, step, ticks);
}

/* Decide whether axis labels are wanted and, if so, how much margin to
 * reserve and which ticks to draw. Reads geometry/ranges/flags from u
 * (before any inset); title / axis-name strings come from `config` (may
 * be NULL). */
static struct yplot_label_plan yplot_plan_labels(const struct yetty_yplot_uniforms *u,
                                                 const struct yetty_yplot_render_config *config)
{
    struct yplot_label_plan plan = {0};
    if (!(u->flags & YETTY_YPLOT_FLAG_LABELS)) {
        return plan;
    }
    double x_range = (double)u->x_max - (double)u->x_min;
    double y_range = (double)u->y_max - (double)u->y_min;
    if (!(x_range > 0.0) || !(y_range > 0.0)) {
        return plan;
    }
    plan.x_log = (u->flags & YETTY_YPLOT_FLAG_XLOG) != 0u;
    plan.y_log = (u->flags & YETTY_YPLOT_FLAG_YLOG) != 0u;

    float font_size = u->bounds_h * 0.06f;
    if (font_size < 9.0f) {
        font_size = 9.0f;
    } else if (font_size > 14.0f) {
        font_size = 14.0f;
    }
    float gap = font_size * 0.5f;

    /* Target divisions scale with available pixels so small figures get
     * fewer ticks and large ones get more, without crowding. */
    int x_divisions = (int)(u->bounds_w / 70.0f);
    if (x_divisions < 2) {
        x_divisions = 2;
    } else if (x_divisions > 8) {
        x_divisions = 8;
    }
    int y_divisions = (int)(u->bounds_h / (font_size * 2.5f));
    if (y_divisions < 2) {
        y_divisions = 2;
    } else if (y_divisions > 6) {
        y_divisions = 6;
    }

    if (plan.x_log) {
        yplot_log_ticks(u->x_min, u->x_max, &plan.x_ticks);
    } else {
        plan.x_step = yplot_nice_step(x_range, x_divisions);
        yplot_linear_ticks(u->x_min, u->x_max, plan.x_step, &plan.x_ticks);
    }
    if (plan.y_log) {
        yplot_log_ticks(u->y_min, u->y_max, &plan.y_ticks);
    } else {
        plan.y_step = yplot_nice_step(y_range, y_divisions);
        yplot_linear_ticks(u->y_min, u->y_max, plan.y_step, &plan.y_ticks);
    }

    /* Widest y tick label sets the left margin. */
    float widest = 0.0f;
    char text[32];
    for (uint32_t i = 0; i < plan.y_ticks.count; i++) {
        yplot_format_tick(plan.y_ticks.values[i],
                          plan.y_log ? fabs(plan.y_ticks.values[i]) : plan.y_step, text,
                          sizeof text);
        float width = yplot_label_width(text, font_size);
        if (width > widest) {
            widest = width;
        }
    }

    float left_margin = widest + gap + 4.0f;
    float bottom_margin = font_size + gap + 4.0f;

    /* Figure chrome strips: title above everything, an axis-name line above
     * the y ticks, an axis-name line under the x ticks. All gated behind
     * FLAG_LABELS like the tick labels themselves. */
    plan.have_title = config && config->title && config->title[0];
    plan.have_x_label = config && config->x_label && config->x_label[0];
    plan.have_y_label = config && config->y_label && config->y_label[0];
    float title_font = u->bounds_h * 0.08f;
    if (title_font < 11.0f) {
        title_font = 11.0f;
    } else if (title_font > 18.0f) {
        title_font = 18.0f;
    }
    plan.title_font = title_font;
    plan.top_margin = 0.0f;
    if (plan.have_title) {
        plan.top_margin += title_font * 1.5f;
    }
    if (plan.have_y_label) {
        plan.top_margin += font_size * 1.4f;
    }
    plan.x_label_margin = plan.have_x_label ? font_size * 1.5f : 0.0f;

    /* Never let labels consume more than 40% of an axis, and skip them
     * entirely when the figure is too small to keep a usable plot area. */
    if (left_margin > u->bounds_w * 0.4f) {
        left_margin = u->bounds_w * 0.4f;
    }
    float vertical_chrome = bottom_margin + plan.top_margin + plan.x_label_margin;
    if (vertical_chrome > u->bounds_h * 0.5f) {
        /* Drop the optional strips first; tick labels are the priority. */
        plan.have_title = false;
        plan.have_x_label = false;
        plan.have_y_label = false;
        plan.top_margin = 0.0f;
        plan.x_label_margin = 0.0f;
        vertical_chrome = bottom_margin;
    }
    if (bottom_margin > u->bounds_h * 0.4f) {
        bottom_margin = u->bounds_h * 0.4f;
        vertical_chrome = bottom_margin + plan.top_margin + plan.x_label_margin;
    }
    if (u->bounds_w - left_margin < 20.0f || u->bounds_h - vertical_chrome < 20.0f) {
        return plan;
    }

    plan.enabled = true;
    plan.font_size = font_size;
    plan.gap = gap;
    plan.left_margin = left_margin;
    plan.bottom_margin = bottom_margin;
    plan.color = 0xFFA8A79Fu; /* brand secondary text #9FA7A8 */
    return plan;
}

/* Add one label as a TEXT prim (default MSDF font: font_id -1). */
static struct yetty_ycore_void_result yplot_add_label(struct yetty_ydraw_drawable_list *list,
                                                      float x, float baseline, const char *text,
                                                      float font_size, uint32_t color)
{
    size_t length = strlen(text);
    struct yetty_ycore_buffer view = {
        .data = (uint8_t *)(uintptr_t)text,
        .capacity = length,
        .size = length,
    };
    return yetty_ydraw_drawable_list_add_text(list, x, baseline, &view, font_size, color, 0, -1,
                                              0.0f);
}

/* Emit tick-number labels for both axes into `list`. `u` holds the INSET
 * plot rect (bounds_* already shrunk by the reserved margins) and the axis
 * ranges; `figure_max_x` is the pre-inset right edge, used to keep the last
 * x label from spilling past the figure. */
static struct yetty_ycore_void_result yplot_emit_axis_labels(struct yetty_ydraw_drawable_list *list,
                                                             const struct yetty_yplot_uniforms *u,
                                                             const struct yplot_label_plan *plan,
                                                             float figure_max_x)
{
    float plot_x = u->bounds_x;
    float plot_y = u->bounds_y;
    float plot_w = u->bounds_w;
    float plot_h = u->bounds_h;
    char text[32];

    /* X axis: ticks along the bottom margin, centered under each tick. */
    for (uint32_t i = 0; i < plan->x_ticks.count; i++) {
        double value = plan->x_ticks.values[i];
        double fraction = yplot_axis_fraction(value, u->x_min, u->x_max, plan->x_log);
        float screen_x = plot_x + (float)fraction * plot_w;
        yplot_format_tick(value, plan->x_log ? fabs(value) : plan->x_step, text, sizeof text);
        float width = yplot_label_width(text, plan->font_size);
        float text_x = screen_x - width * 0.5f;
        if (text_x < 0.0f) {
            text_x = 0.0f;
        } else if (text_x + width > figure_max_x) {
            text_x = figure_max_x - width;
        }
        float baseline = plot_y + plot_h + plan->gap + plan->font_size * 0.8f;
        struct yetty_ycore_void_result label_res =
            yplot_add_label(list, text_x, baseline, text, plan->font_size, plan->color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, label_res, "yplot: x-axis label");
    }

    /* Y axis: ticks up the left margin, right-aligned to the plot edge and
     * vertically centered. plotUV.y grows downward, so yMax is at the top. */
    for (uint32_t i = 0; i < plan->y_ticks.count; i++) {
        double value = plan->y_ticks.values[i];
        double fraction = 1.0 - yplot_axis_fraction(value, u->y_min, u->y_max, plan->y_log);
        float screen_y = plot_y + (float)fraction * plot_h;
        yplot_format_tick(value, plan->y_log ? fabs(value) : plan->y_step, text, sizeof text);
        float width = yplot_label_width(text, plan->font_size);
        float text_x = plot_x - plan->gap - width;
        if (text_x < 0.0f) {
            text_x = 0.0f;
        }
        float baseline = screen_y + plan->font_size * 0.35f;
        struct yetty_ycore_void_result label_res =
            yplot_add_label(list, text_x, baseline, text, plan->font_size, plan->color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, label_res, "yplot: y-axis label");
    }
    return YETTY_OK_VOID();
}

/* Emit the figure chrome strips reserved by the plan: the title (centered
 * over the full figure), the y-axis name (above the tick column), and the
 * x-axis name (centered under the x tick labels). `figure_*` is the
 * pre-inset figure rect; `u` holds the inset plot rect. */
static struct yetty_ycore_void_result yplot_emit_chrome(
    struct yetty_ydraw_drawable_list *list, const struct yetty_yplot_uniforms *u,
    const struct yplot_label_plan *plan, float figure_x, float figure_y, float figure_w,
    float figure_h, const struct yetty_yplot_render_config *config)
{
    float strip_y = figure_y;

    if (plan->have_title) {
        float width = yplot_label_width(config->title, plan->title_font);
        float text_x = figure_x + (figure_w - width) * 0.5f;
        if (text_x < figure_x) {
            text_x = figure_x;
        }
        float baseline = strip_y + plan->title_font;
        struct yetty_ycore_void_result title_res =
            yplot_add_label(list, text_x, baseline, config->title, plan->title_font, 0xFFE4E5E0u);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, title_res, "yplot: title");
        strip_y += plan->title_font * 1.5f;
    }

    if (plan->have_y_label) {
        float baseline = strip_y + plan->font_size;
        struct yetty_ycore_void_result y_label_res = yplot_add_label(
            list, figure_x, baseline, config->y_label, plan->font_size, plan->color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, y_label_res, "yplot: y-axis name");
    }

    if (plan->have_x_label) {
        float width = yplot_label_width(config->x_label, plan->font_size);
        float text_x = u->bounds_x + (u->bounds_w - width) * 0.5f;
        if (text_x < figure_x) {
            text_x = figure_x;
        }
        float baseline = figure_y + figure_h - plan->font_size * 0.35f;
        struct yetty_ycore_void_result x_label_res =
            yplot_add_label(list, text_x, baseline, config->x_label, plan->font_size, plan->color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, x_label_res, "yplot: x-axis name");
    }
    return YETTY_OK_VOID();
}

/* Width of the right margin needed to host a vertical legend for `entries`.
 * Whether a legend is warranted at all is the caller's decision (legend
 * mode gating lives in yplot_emit_prim). */
static float yplot_legend_margin(const struct yplot_legend_entry *entries, size_t count,
                                 float font_size)
{
    if (count == 0) {
        return 0.0f;
    }
    float pad = font_size * 0.4f;
    float swatch_width = font_size * 1.0f;
    float gap = font_size * 0.4f;
    float widest_name = 0.0f;
    for (size_t i = 0; i < count; i++) {
        float name_width = entries[i].name ? yplot_label_width(entries[i].name, font_size) : 0.0f;
        if (name_width > widest_name) {
            widest_name = name_width;
        }
    }
    return pad + swatch_width + gap + widest_name + pad;
}

/* Curve colors are stored 0xAARRGGBB for the plot shader (yplot_unpack_color
 * reads R at bits 16-23). The SDF box path used for the legend swatch reads the
 * opposite order (R at bits 0-7), so swap R and B to render the swatch in the
 * same visible color as its curve. */
static uint32_t yplot_color_swap_rb(uint32_t color)
{
    return (color & 0xFF00FF00u) | ((color >> 16) & 0x000000FFu) | ((color & 0x000000FFu) << 16);
}

/* Draw the legend as a vertical stack in the reserved right margin: one row per
 * curve, a filled color swatch followed by the curve name. The strip sits
 * outside the plot rect (the shader discards those fragments), so no draw-order
 * juggling against the plot is needed. `margin_left` is the x of the strip's
 * left edge (the inset plot's right edge). */
static struct yetty_ycore_void_result yplot_emit_legend(struct yetty_ydraw_drawable_list *list,
                                                        const struct yetty_yplot_uniforms *u,
                                                        const struct yplot_label_plan *plan,
                                                        float margin_left,
                                                        const struct yplot_legend_entry *entries,
                                                        size_t count)
{
    float font_size = plan->font_size;
    float pad = font_size * 0.4f;
    float swatch_width = font_size * 1.0f;
    float swatch_height = font_size * 0.5f;
    float gap = font_size * 0.4f;
    float row_height = font_size * 1.4f;
    float swatch_x = margin_left + pad;
    float top = u->bounds_y + pad;

    for (size_t i = 0; i < count; i++) {
        float row_center = top + (float)i * row_height + row_height * 0.5f;

        struct yetty_ysdf_box swatch = {
            .center_x = swatch_x + swatch_width * 0.5f,
            .center_y = row_center,
            .half_width = swatch_width * 0.5f,
            .half_height = swatch_height * 0.5f,
            .corner_radius = swatch_height * 0.25f,
        };
        struct yetty_ycore_void_result swatch_res = yetty_ydraw_drawable_list_add_cmd_add_box(
            list, 0u, (uint32_t)(1000 + i * 2), yplot_color_swap_rb(entries[i].color), 0u, 0.0f,
            &swatch);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, swatch_res, "yplot: legend swatch");

        if (entries[i].name && entries[i].name[0]) {
            float text_x = swatch_x + swatch_width + gap;
            float baseline = row_center + font_size * 0.35f;
            struct yetty_ycore_void_result text_res =
                yplot_add_label(list, text_x, baseline, entries[i].name, font_size, 0xFFE4E5E0u);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, text_res, "yplot: legend name");
        }
    }
    return YETTY_OK_VOID();
}

enum { YPLOT_COLORBAR_STEPS = 32 };

/* Format the three colorbar tick values (max / mid / min, top to bottom). */
static void yplot_colorbar_labels(const struct yetty_yplot_uniforms *u, char out[3][32])
{
    double span = fabs((double)u->field_max - (double)u->field_min);
    double step = span > 0.0 ? span * 0.5 : 1.0;
    yplot_format_tick(u->field_max, step, out[0], 32);
    yplot_format_tick(((double)u->field_min + (double)u->field_max) * 0.5, step, out[1], 32);
    yplot_format_tick(u->field_min, step, out[2], 32);
}

/* Right-margin width needed for the colorbar strip + its value labels. */
static float yplot_colorbar_margin(const struct yetty_yplot_uniforms *u,
                                   const struct yplot_label_plan *plan)
{
    char labels[3][32];
    yplot_colorbar_labels(u, labels);
    float widest = 0.0f;
    for (int i = 0; i < 3; i++) {
        float width = yplot_label_width(labels[i], plan->font_size);
        if (width > widest) {
            widest = width;
        }
    }
    float pad = plan->font_size * 0.4f;
    float bar_width = plan->font_size * 0.9f;
    float gap = plan->font_size * 0.4f;
    return pad + bar_width + gap + widest + pad;
}

/* Draw the colorbar in the reserved right margin: a vertical gradient strip
 * (stacked SDF boxes sampled from the C colormap twin, field_max at the
 * top) plus max / mid / min value labels beside it. */
static struct yetty_ycore_void_result yplot_emit_colorbar(struct yetty_ydraw_drawable_list *list,
                                                          const struct yetty_yplot_uniforms *u,
                                                          const struct yplot_label_plan *plan,
                                                          float margin_left)
{
    float font_size = plan->font_size;
    float pad = font_size * 0.4f;
    float bar_width = font_size * 0.9f;
    float gap = font_size * 0.4f;
    float bar_x = margin_left + pad;
    float bar_top = u->bounds_y;
    float bar_height = u->bounds_h;

    for (int i = 0; i < YPLOT_COLORBAR_STEPS; i++) {
        /* Top segment carries the top of the range. Slight overlap between
         * segments avoids background seams. */
        float t = 1.0f - ((float)i + 0.5f) / (float)YPLOT_COLORBAR_STEPS;
        float segment_top = bar_top + bar_height * (float)i / (float)YPLOT_COLORBAR_STEPS;
        float segment_height = bar_height / (float)YPLOT_COLORBAR_STEPS + 0.5f;
        struct yetty_ysdf_box segment = {
            .center_x = bar_x + bar_width * 0.5f,
            .center_y = segment_top + segment_height * 0.5f,
            .half_width = bar_width * 0.5f,
            .half_height = segment_height * 0.5f,
            .corner_radius = 0.0f,
        };
        struct yetty_ycore_void_result segment_res = yetty_ydraw_drawable_list_add_cmd_add_box(
            list, 0u, (uint32_t)(2000 + i), yplot_colormap_sample(u->colormap_id, t), 0u, 0.0f,
            &segment);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, segment_res, "yplot: colorbar segment");
    }

    char labels[3][32];
    yplot_colorbar_labels(u, labels);
    float label_x = bar_x + bar_width + gap;
    float baselines[3] = {
        bar_top + font_size * 0.8f,
        bar_top + bar_height * 0.5f + font_size * 0.35f,
        bar_top + bar_height,
    };
    for (int i = 0; i < 3; i++) {
        struct yetty_ycore_void_result label_res =
            yplot_add_label(list, label_x, baselines[i], labels[i], font_size, plan->color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, label_res, "yplot: colorbar label");
    }
    return YETTY_OK_VOID();
}

/* Pack uniforms + buffers into a fresh ydraw buffer carrying one yplot prim,
 * plus MSDF tick labels on both axes, the title / axis-name chrome, and a
 * curve legend, all gated on FLAG_LABELS. `legend_entries` names each curve
 * for the legend (may be NULL); `config` carries the chrome strings and the
 * legend mode (may be NULL). */
/* Emit the plot prim + chrome INTO `dest` at (origin_x, origin_y). The caller
 * owns the list and its scene bounds; on error the partial prims are left for
 * the caller to discard. This is the single emission body every frontend
 * funnels through (via yetty_yplot_emit_into). */
static struct yetty_ycore_void_result yplot_emit_into_list(
    struct yetty_ydraw_drawable_list *dest, float origin_x, float origin_y,
    const struct yetty_yplot_uniforms *u_in, const struct yetty_yplot_buffers *bufs,
    const struct yplot_legend_entry *legend_entries, size_t legend_count,
    const struct yetty_yplot_render_config *config)
{
    struct yetty_yplot_uniforms u = *u_in;
    u.bounds_x = origin_x;
    u.bounds_y = origin_y;

    /* A log axis over a non-positive range has no defined mapping — reject
     * it here, the shared choke point of every render entry path. */
    if ((u.flags & YETTY_YPLOT_FLAG_XLOG) && !(u.x_min > 0.0f && u.x_max > u.x_min)) {
        return YETTY_ERR(yetty_ycore_void, "yplot: x log scale requires 0 < x_min < x_max");
    }
    if ((u.flags & YETTY_YPLOT_FLAG_YLOG) && !(u.y_min > 0.0f && u.y_max > u.y_min)) {
        return YETTY_ERR(yetty_ycore_void, "yplot: y log scale requires 0 < y_min < y_max");
    }

    /* Full figure extents BEFORE any label inset — the drawable list scene
     * must span these so the bottom/left labels in the reserved margins are
     * not clipped and the scrollback figure reserves enough rows. */
    float figure_x = u.bounds_x;
    float figure_y = u.bounds_y;
    float figure_w = u.bounds_w;
    float figure_h = u.bounds_h;
    float figure_max_x = u.bounds_x + u.bounds_w;

    /* Reserve label margins by insetting the plot rect; the shader positions
     * and clips the plot to bounds_*, so the margins stay transparent for
     * the label text prims. */
    struct yplot_label_plan plan = yplot_plan_labels(&u, config);

    /* Field plots get a colorbar in the right margin instead of a legend
     * (they render exactly one function, so there is nothing to name). */
    bool is_field = (u.flags & YETTY_YPLOT_FLAG_FIELD) != 0u;
    float colorbar_margin = 0.0f;
    bool show_colorbar = false;
    if (plan.enabled && is_field) {
        colorbar_margin = yplot_colorbar_margin(&u, &plan);
        float plot_width_left = u.bounds_w - plan.left_margin - colorbar_margin;
        if (colorbar_margin <= u.bounds_w * 0.4f && plot_width_left >= 20.0f) {
            show_colorbar = true;
        } else {
            colorbar_margin = 0.0f;
        }
    }

    /* A legend is drawn only alongside the axis labels (same FLAG_LABELS
     * gate). By default it needs two named curves to be worth its margin;
     * the config can force it on for one curve or suppress it entirely. */
    size_t legend_minimum = 2;
    if (config && config->legend_mode == YETTY_YPLOT_LEGEND_ON) {
        legend_minimum = 1;
    } else if (config && config->legend_mode == YETTY_YPLOT_LEGEND_OFF) {
        legend_minimum = SIZE_MAX;
    }
    float legend_margin = 0.0f;
    bool show_legend = false;
    if (plan.enabled && !is_field && legend_entries && legend_count >= legend_minimum) {
        legend_margin = yplot_legend_margin(legend_entries, legend_count, plan.font_size);
        float plot_width_left = u.bounds_w - plan.left_margin - legend_margin;
        if (legend_margin > 0.0f && legend_margin <= u.bounds_w * 0.4f &&
            plot_width_left >= 20.0f) {
            show_legend = true;
        } else {
            legend_margin = 0.0f;
        }
    }

    if (plan.enabled) {
        u.bounds_x += plan.left_margin;
        u.bounds_w -= plan.left_margin;
        u.bounds_y += plan.top_margin;
        u.bounds_h -= plan.top_margin + plan.bottom_margin + plan.x_label_margin;
        /* Tell the shader where the labelled ticks sit so the grid lands
         * under them (0 = log axis or no tick info: shader handles both). */
        u.x_step = plan.x_log ? 0.0f : (float)plan.x_step;
        u.y_step = plan.y_log ? 0.0f : (float)plan.y_step;
    }
    if (show_legend) {
        u.bounds_w -= legend_margin;
    }
    if (show_colorbar) {
        u.bounds_w -= colorbar_margin;
    }

    size_t required = yetty_yplot_uniforms_serialized_size(&u, bufs);
    uint8_t *drawable_buf = malloc(required);
    if (!drawable_buf) {
        return YETTY_ERR(yetty_ycore_void, "yplot: prim alloc failed");
    }
    struct yetty_ycore_size_result ser =
        yetty_yplot_uniforms_serialize(&u, bufs, drawable_buf, required);
    if (YETTY_IS_ERR(ser)) {
        free(drawable_buf);
        return YETTY_ERR(yetty_ycore_void, "yplot: serialize failed", ser);
    }

    struct yetty_ydraw_id_result idr =
        yetty_ydraw_drawable_list_add_prim(dest, drawable_buf, required);
    free(drawable_buf);
    if (YETTY_IS_ERR(idr)) {
        return YETTY_ERR(yetty_ycore_void, "yplot: ydraw add_prim failed", idr);
    }

    if (plan.enabled) {
        struct yetty_ycore_void_result label_res =
            yplot_emit_axis_labels(dest, &u, &plan, figure_max_x);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, label_res, "yplot: axis labels");
        if (plan.have_title || plan.have_x_label || plan.have_y_label) {
            struct yetty_ycore_void_result chrome_res =
                yplot_emit_chrome(dest, &u, &plan, figure_x, figure_y, figure_w, figure_h, config);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, chrome_res, "yplot: figure chrome");
        }
    }
    if (show_legend) {
        /* The strip's left edge is the inset plot's right edge. */
        float margin_left = u.bounds_x + u.bounds_w;
        struct yetty_ycore_void_result legend_res =
            yplot_emit_legend(dest, &u, &plan, margin_left, legend_entries, legend_count);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, legend_res, "yplot: legend");
    }
    if (show_colorbar) {
        float margin_left = u.bounds_x + u.bounds_w;
        struct yetty_ycore_void_result colorbar_res =
            yplot_emit_colorbar(dest, &u, &plan, margin_left);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, colorbar_res, "yplot: colorbar");
    }
    return YETTY_OK_VOID();
}

/* Emit a complete render plan into an existing list at an origin — the public
 * shared entry (yplot/resolve.h). Rebuilds the private legend view and a chrome
 * config from the plan's flat fields, then runs the one emission body. */
struct yetty_ycore_void_result yetty_yplot_emit_into(const struct yetty_yplot_render_plan *plan,
                                                     struct yetty_ydraw_drawable_list *dest,
                                                     float origin_x, float origin_y)
{
    if (!plan || !dest) {
        return YETTY_ERR(yetty_ycore_void, "yplot emit_into: NULL plan or dest");
    }
    if (!(isfinite(origin_x) && isfinite(origin_y))) {
        return YETTY_ERR(yetty_ycore_void, "yplot emit_into: non-finite origin");
    }
    /* Validate plan invariants before mutating dest — a plan is valid or not. */
    if (!(isfinite(plan->uniforms.bounds_w) && plan->uniforms.bounds_w > 0.0f &&
          isfinite(plan->uniforms.bounds_h) && plan->uniforms.bounds_h > 0.0f)) {
        return YETTY_ERR(yetty_ycore_void,
                         "yplot emit_into: figure dimensions must be positive finite");
    }
    /* Every pointer/count pair must be consistent — the serializer sizes on the
     * declared lengths but skips the copy when the pointer is NULL, which would
     * emit malformed, partly-uninitialized wire bytes. Bound the sizes too so
     * the serialized-size arithmetic cannot overflow size_t (the caps sit far
     * above any real plot; the shared plan builder stays well under them). */
    if (plan->buffers.bytecode == NULL && plan->buffers.bytecode_len > 0) {
        return YETTY_ERR(yetty_ycore_void, "yplot emit_into: bytecode_len > 0 with NULL bytecode");
    }
    if (plan->buffers.bytecode_len > (1u << 20) || plan->buffers.data_count > 256) {
        return YETTY_ERR(yetty_ycore_void, "yplot emit_into: plan exceeds size bounds");
    }
    if (plan->buffers.data == NULL && plan->buffers.data_count > 0) {
        return YETTY_ERR(yetty_ycore_void, "yplot emit_into: data_count > 0 with NULL buffers");
    }
    for (size_t i = 0; i < plan->buffers.data_count; i++) {
        if (plan->buffers.data[i].samples == NULL && plan->buffers.data[i].count > 0) {
            return YETTY_ERR(yetty_ycore_void,
                             "yplot emit_into: data buffer count > 0 with NULL samples");
        }
        if (plan->buffers.data[i].count > (1u << 22)) {
            return YETTY_ERR(yetty_ycore_void, "yplot emit_into: data buffer too large");
        }
    }
    if (plan->legend_count > YETTY_YEXPR_MAX_PLOT_DEFS + 8) {
        return YETTY_ERR(yetty_ycore_void, "yplot emit_into: legend_count exceeds capacity");
    }
    size_t legend_count = plan->legend_count;
    struct yplot_legend_entry legend[YETTY_YEXPR_MAX_PLOT_DEFS + 8];
    for (size_t i = 0; i < legend_count; i++) {
        legend[i].name = plan->legend_names[i];
        legend[i].color = plan->legend_colors[i];
    }
    struct yetty_yplot_render_config config = {
        .title = plan->title,
        .x_label = plan->x_label,
        .y_label = plan->y_label,
        .legend_mode = plan->legend_mode,
    };
    return yplot_emit_into_list(dest, origin_x, origin_y, &plan->uniforms, &plan->buffers, legend,
                                legend_count, &config);
}

/* Emit a plan into a fresh drawable list spanning the figure extents (the label
 * margins are reserved inside these bounds, so nothing is clipped) — the
 * standalone-render convenience used by both the expression and precompiled
 * entry points. */
static struct yetty_ydraw_drawable_list_result yplot_emit_to_new_list(
    const struct yetty_yplot_render_plan *plan)
{
    const struct yetty_yplot_uniforms *u = &plan->uniforms;
    struct yetty_ydraw_drawable_list_config list_cfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = u->bounds_x + u->bounds_w,
        .scene_max_y = u->bounds_y + u->bounds_h,
    };
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(&list_cfg);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, list_res, "yplot: list create");

    struct yetty_ycore_void_result emit_res =
        yetty_yplot_emit_into(plan, list_res.value, u->bounds_x, u->bounds_y);
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ydraw_drawable_list_destroy(list_res.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: emit", emit_res);
    }
    return YETTY_OK(yetty_ydraw_drawable_list, list_res.value);
}

/* Map an on/off keyword. Returns 1 on success. */
static int yplot_attr_bool(const char *value, bool *out)
{
    if (!strcmp(value, "on") || !strcmp(value, "true") || !strcmp(value, "yes") ||
        !strcmp(value, "1")) {
        *out = true;
        return 1;
    }
    if (!strcmp(value, "off") || !strcmp(value, "false") || !strcmp(value, "no") ||
        !strcmp(value, "0")) {
        *out = false;
        return 1;
    }
    return 0;
}

/*=============================================================================
 * Expression resolution — the shared boundary (yplot/resolve.h). Merges a
 * parsed expression with presence-aware caller overrides under an explicit
 * precedence, validating semantics over the whole parsed expression.
 *===========================================================================*/

static void yplot_resolved_defaults(struct yetty_yplot_resolved *out)
{
    *out = (struct yetty_yplot_resolved){0};
    out->grid = true;
    out->axes = true;
    out->labels = true;
    out->legend = YETTY_YPLOT_LEGEND_AUTO;
    out->colormap = YETTY_YPLOT_COLORMAP_VIRIDIS;
}

static void yplot_resolved_apply_options(struct yetty_yplot_resolved *out,
                                         const struct yetty_yplot_options *options)
{
    if (!options) {
        return;
    }
    uint32_t present = options->present;
    if (present & YETTY_YPLOT_OPT_WIDTH) {
        out->width = options->width;
        out->has_width = true;
    }
    if (present & YETTY_YPLOT_OPT_HEIGHT) {
        out->height = options->height;
        out->has_height = true;
    }
    if (present & YETTY_YPLOT_OPT_X_RANGE) {
        out->x_min = options->x_min;
        out->x_max = options->x_max;
        out->has_x_range = true;
    }
    if (present & YETTY_YPLOT_OPT_Y_RANGE) {
        out->y_min = options->y_min;
        out->y_max = options->y_max;
        out->has_y_range = true;
    }
    if (present & YETTY_YPLOT_OPT_TITLE) {
        out->title = options->title;
    }
    if (present & YETTY_YPLOT_OPT_X_LABEL) {
        out->x_label = options->x_label;
    }
    if (present & YETTY_YPLOT_OPT_Y_LABEL) {
        out->y_label = options->y_label;
    }
    if (present & YETTY_YPLOT_OPT_GRID) {
        out->grid = options->grid;
    }
    if (present & YETTY_YPLOT_OPT_AXES) {
        out->axes = options->axes;
    }
    if (present & YETTY_YPLOT_OPT_LABELS) {
        out->labels = options->labels;
    }
    if (present & YETTY_YPLOT_OPT_X_LOG) {
        out->x_log = options->x_log;
    }
    if (present & YETTY_YPLOT_OPT_Y_LOG) {
        out->y_log = options->y_log;
    }
    if (present & YETTY_YPLOT_OPT_LEGEND) {
        out->legend = options->legend;
    }
    if (present & YETTY_YPLOT_OPT_COLORMAP) {
        out->colormap = options->colormap;
    }
    if (present & YETTY_YPLOT_OPT_FIELD) {
        out->field_min = options->field_min;
        out->field_max = options->field_max;
        out->has_field = true;
    }
}

static bool yplot_is_declared_curve(const struct yetty_yexpr_plot_expr *expr, const char *name)
{
    for (uint32_t i = 0; i < expr->def_count; i++) {
        if (!strcmp(expr->defs[i].name, name)) {
            return true;
        }
    }
    return false;
}

static bool yplot_is_declared_buffer(const struct yetty_yexpr_plot_expr *expr, const char *name)
{
    for (uint32_t i = 0; i < expr->buffer_count; i++) {
        if (!strcmp(expr->buffers[i].name, name)) {
            return true;
        }
    }
    return false;
}

/* Apply + validate the parsed expression's figure/axis/curve attributes onto
 * `out`. Runs over the fully-parsed expression, so validation is independent
 * of the order attributes and curve/buffer declarations appear in the source.
 * Title/label pointers borrow into `expr`, which must outlive the emit. */
static struct yetty_ycore_void_result yplot_resolved_apply_expr(
    struct yetty_yplot_resolved *out, const struct yetty_yexpr_plot_expr *expr)
{
    if (expr->fig_present & YETTY_YEXPR_FIG_WIDTH) {
        out->width = expr->fig_width;
        out->has_width = true;
    }
    if (expr->fig_present & YETTY_YEXPR_FIG_HEIGHT) {
        out->height = expr->fig_height;
        out->has_height = true;
    }
    if (expr->fig_present & YETTY_YEXPR_FIG_FIELD) {
        out->field_min = expr->field_min;
        out->field_max = expr->field_max;
        out->has_field = true;
    }
    if (expr->has_x_range) {
        out->x_min = expr->x_min;
        out->x_max = expr->x_max;
        out->has_x_range = true;
    }
    if (expr->has_y_range) {
        out->y_min = expr->y_min;
        out->y_max = expr->y_max;
        out->has_y_range = true;
    }
    if (expr->has_view) {
        out->x_min = expr->view_x_min;
        out->x_max = expr->view_x_max;
        out->y_min = expr->view_y_min;
        out->y_max = expr->view_y_max;
        out->has_x_range = true;
        out->has_y_range = true;
    }

    for (uint32_t i = 0; i < expr->attr_count; i++) {
        const struct yetty_yexpr_plot_attr *attr = &expr->attrs[i];
        const char *target = attr->plot_name;
        const char *name = attr->attr_name;
        const char *value = attr->value;
        bool is_figure =
            !strcmp(target, "plot") || !strcmp(target, "fig") || !strcmp(target, "figure");
        bool is_x_axis = !strcmp(target, "x");
        bool is_y_axis = !strcmp(target, "y");

        if (is_figure) {
            if (!strcmp(name, "title")) {
                out->title = attr->value;
            } else if (!strcmp(name, "grid") || !strcmp(name, "axes") || !strcmp(name, "labels")) {
                bool on;
                if (!yplot_attr_bool(value, &on)) {
                    return YETTY_ERR(yetty_ycore_void,
                                     "yplot: @plot grid/axes/labels expects on/off");
                }
                if (!strcmp(name, "grid")) {
                    out->grid = on;
                } else if (!strcmp(name, "axes")) {
                    out->axes = on;
                } else {
                    out->labels = on;
                }
            } else if (!strcmp(name, "legend")) {
                if (!strcmp(value, "auto")) {
                    out->legend = YETTY_YPLOT_LEGEND_AUTO;
                } else if (!strcmp(value, "on")) {
                    out->legend = YETTY_YPLOT_LEGEND_ON;
                } else if (!strcmp(value, "off")) {
                    out->legend = YETTY_YPLOT_LEGEND_OFF;
                } else {
                    return YETTY_ERR(yetty_ycore_void, "yplot: @plot.legend expects auto/on/off");
                }
            } else if (!strcmp(name, "colormap")) {
                if (!strcmp(value, "viridis")) {
                    out->colormap = YETTY_YPLOT_COLORMAP_VIRIDIS;
                } else if (!strcmp(value, "plasma")) {
                    out->colormap = YETTY_YPLOT_COLORMAP_PLASMA;
                } else if (!strcmp(value, "magma")) {
                    out->colormap = YETTY_YPLOT_COLORMAP_MAGMA;
                } else if (!strcmp(value, "inferno")) {
                    out->colormap = YETTY_YPLOT_COLORMAP_INFERNO;
                } else {
                    return YETTY_ERR(yetty_ycore_void,
                                     "yplot: @plot.colormap expects viridis/plasma/magma/inferno");
                }
            } else {
                return YETTY_ERR(yetty_ycore_void, "yplot: unknown @plot attribute");
            }
        } else if (is_x_axis || is_y_axis) {
            if (!strcmp(name, "label")) {
                if (is_x_axis) {
                    out->x_label = attr->value;
                } else {
                    out->y_label = attr->value;
                }
            } else if (!strcmp(name, "scale")) {
                bool log_scale;
                if (!strcmp(value, "log")) {
                    log_scale = true;
                } else if (!strcmp(value, "linear")) {
                    log_scale = false;
                } else {
                    return YETTY_ERR(yetty_ycore_void, "yplot: @x/@y.scale expects linear/log");
                }
                if (is_x_axis) {
                    out->x_log = log_scale;
                } else {
                    out->y_log = log_scale;
                }
            } else {
                return YETTY_ERR(yetty_ycore_void, "yplot: unknown axis attribute (@x/@y)");
            }
        } else if (yplot_is_declared_curve(expr, target) ||
                   yplot_is_declared_buffer(expr, target)) {
            /* `color` is the sole supported curve/buffer attribute. Validate the
             * value with the same parser emission uses, so an unparseable color
             * is an error here — not a silent palette-default at emit time. */
            if (strcmp(name, "color") != 0) {
                return YETTY_ERR(yetty_ycore_void, "yplot: unknown curve/buffer attribute");
            }
            uint32_t curve_color;
            if (!parse_hex_color(value, &curve_color)) {
                return YETTY_ERR(yetty_ycore_void, "yplot: invalid curve color (expected #RRGGBB)");
            }
        } else {
            return YETTY_ERR(yetty_ycore_void, "yplot: attribute targets an unknown name");
        }
    }
    return YETTY_OK_VOID();
}

/* Validate presence-aware caller overrides. The facade passes these directly,
 * so they get the same rigor as expression attributes: no unknown presence
 * bits, positive finite dimensions, finite ordered ranges, in-range enums. */
static struct yetty_ycore_void_result yplot_options_validate(
    const struct yetty_yplot_options *options)
{
    if (!options) {
        return YETTY_OK_VOID();
    }
    const uint32_t known =
        YETTY_YPLOT_OPT_WIDTH | YETTY_YPLOT_OPT_HEIGHT | YETTY_YPLOT_OPT_X_RANGE |
        YETTY_YPLOT_OPT_Y_RANGE | YETTY_YPLOT_OPT_TITLE | YETTY_YPLOT_OPT_X_LABEL |
        YETTY_YPLOT_OPT_Y_LABEL | YETTY_YPLOT_OPT_GRID | YETTY_YPLOT_OPT_AXES |
        YETTY_YPLOT_OPT_LABELS | YETTY_YPLOT_OPT_X_LOG | YETTY_YPLOT_OPT_Y_LOG |
        YETTY_YPLOT_OPT_LEGEND | YETTY_YPLOT_OPT_COLORMAP | YETTY_YPLOT_OPT_FIELD;
    if (options->present & ~known) {
        return YETTY_ERR(yetty_ycore_void, "yplot options: unknown presence bit");
    }
    if ((options->present & YETTY_YPLOT_OPT_WIDTH) &&
        !(isfinite(options->width) && options->width > 0.0f)) {
        return YETTY_ERR(yetty_ycore_void, "yplot options: width must be positive finite");
    }
    if ((options->present & YETTY_YPLOT_OPT_HEIGHT) &&
        !(isfinite(options->height) && options->height > 0.0f)) {
        return YETTY_ERR(yetty_ycore_void, "yplot options: height must be positive finite");
    }
    if ((options->present & YETTY_YPLOT_OPT_X_RANGE) &&
        !(isfinite(options->x_min) && isfinite(options->x_max) &&
          options->x_min < options->x_max)) {
        return YETTY_ERR(yetty_ycore_void, "yplot options: x range must be finite and ordered");
    }
    if ((options->present & YETTY_YPLOT_OPT_Y_RANGE) &&
        !(isfinite(options->y_min) && isfinite(options->y_max) &&
          options->y_min < options->y_max)) {
        return YETTY_ERR(yetty_ycore_void, "yplot options: y range must be finite and ordered");
    }
    if ((options->present & YETTY_YPLOT_OPT_FIELD) &&
        !(isfinite(options->field_min) && isfinite(options->field_max) &&
          options->field_min < options->field_max)) {
        return YETTY_ERR(yetty_ycore_void, "yplot options: field range must be finite and ordered");
    }
    if ((options->present & YETTY_YPLOT_OPT_LEGEND) &&
        ((int)options->legend < 0 || (int)options->legend > YETTY_YPLOT_LEGEND_OFF)) {
        return YETTY_ERR(yetty_ycore_void, "yplot options: invalid legend mode");
    }
    if ((options->present & YETTY_YPLOT_OPT_COLORMAP) &&
        ((int)options->colormap < 0 || (int)options->colormap > YETTY_YPLOT_COLORMAP_INFERNO)) {
        return YETTY_ERR(yetty_ycore_void, "yplot options: invalid colormap");
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yplot_resolve(const struct yetty_yexpr_plot_expr *expression,
                                                   const struct yetty_yplot_options *options,
                                                   enum yetty_yplot_attr_precedence precedence,
                                                   struct yetty_yplot_resolved *out)
{
    if (!expression || !out) {
        return YETTY_ERR(yetty_ycore_void, "yplot resolve: NULL expression or output");
    }
    if (precedence != YETTY_YPLOT_EXPRESSION_WINS && precedence != YETTY_YPLOT_CONFIG_WINS) {
        return YETTY_ERR(yetty_ycore_void, "yplot resolve: invalid precedence");
    }
    struct yetty_ycore_void_result options_valid = yplot_options_validate(options);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, options_valid, "yplot resolve: invalid options");

    /* Resolve into a local; assign *out only on success so a validation failure
     * never leaves partially-merged state visible. Apply the lower-precedence
     * source first, then let the higher-precedence source overwrite the
     * properties it names. `apply_expr` always runs (and validates). */
    struct yetty_yplot_resolved local;
    yplot_resolved_defaults(&local);
    if (precedence == YETTY_YPLOT_CONFIG_WINS) {
        struct yetty_ycore_void_result expr_res = yplot_resolved_apply_expr(&local, expression);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, expr_res, "yplot resolve: expression");
        yplot_resolved_apply_options(&local, options);
    } else {
        yplot_resolved_apply_options(&local, options);
        struct yetty_ycore_void_result expr_res = yplot_resolved_apply_expr(&local, expression);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, expr_res, "yplot resolve: expression");
    }

    /* Validate the MERGED domain ranges regardless of which source won — a
     * reversed range (from an expression `x=5..1` or a `@view`) has no
     * well-defined axis mapping. Log-axis positivity stays an emission check
     * since it depends on range + scale together. */
    if (local.has_x_range &&
        !(isfinite(local.x_min) && isfinite(local.x_max) && local.x_min < local.x_max)) {
        return YETTY_ERR(yetty_ycore_void, "yplot resolve: x range must be finite and ordered");
    }
    if (local.has_y_range &&
        !(isfinite(local.y_min) && isfinite(local.y_max) && local.y_min < local.y_max)) {
        return YETTY_ERR(yetty_ycore_void, "yplot resolve: y range must be finite and ordered");
    }

    *out = local;
    return YETTY_OK_VOID();
}

/* Map the legacy public render_config to presence-aware options (a non-default
 * field is treated as explicitly present). Lets existing config-driven callers
 * flow through the resolver unchanged, under EXPRESSION_WINS. */
static void yplot_options_from_config(const struct yetty_yplot_render_config *config,
                                      struct yetty_yplot_options *out)
{
    *out = (struct yetty_yplot_options){0};
    if (!config) {
        return;
    }
    if (config->bounds_w > 0.0f) {
        out->present |= YETTY_YPLOT_OPT_WIDTH;
        out->width = config->bounds_w;
    }
    if (config->bounds_h > 0.0f) {
        out->present |= YETTY_YPLOT_OPT_HEIGHT;
        out->height = config->bounds_h;
    }
    if (config->x_min != 0.0f || config->x_max != 0.0f) {
        out->present |= YETTY_YPLOT_OPT_X_RANGE;
        out->x_min = config->x_min;
        out->x_max = config->x_max;
    }
    if (config->y_min != 0.0f || config->y_max != 0.0f) {
        out->present |= YETTY_YPLOT_OPT_Y_RANGE;
        out->y_min = config->y_min;
        out->y_max = config->y_max;
    }
    if (config->title && config->title[0]) {
        out->present |= YETTY_YPLOT_OPT_TITLE;
        out->title = config->title;
    }
    if (config->x_label && config->x_label[0]) {
        out->present |= YETTY_YPLOT_OPT_X_LABEL;
        out->x_label = config->x_label;
    }
    if (config->y_label && config->y_label[0]) {
        out->present |= YETTY_YPLOT_OPT_Y_LABEL;
        out->y_label = config->y_label;
    }
    if (config->flags != 0) {
        out->present |= YETTY_YPLOT_OPT_GRID | YETTY_YPLOT_OPT_AXES | YETTY_YPLOT_OPT_LABELS |
                        YETTY_YPLOT_OPT_X_LOG | YETTY_YPLOT_OPT_Y_LOG;
        out->grid = (config->flags & YETTY_YPLOT_FLAG_GRID) != 0;
        out->axes = (config->flags & YETTY_YPLOT_FLAG_AXES) != 0;
        out->labels = (config->flags & YETTY_YPLOT_FLAG_LABELS) != 0;
        out->x_log = (config->flags & YETTY_YPLOT_FLAG_XLOG) != 0;
        out->y_log = (config->flags & YETTY_YPLOT_FLAG_YLOG) != 0;
    }
    if (config->legend_mode != YETTY_YPLOT_LEGEND_AUTO) {
        out->present |= YETTY_YPLOT_OPT_LEGEND;
        out->legend = config->legend_mode;
    }
    if (config->colormap != YETTY_YPLOT_COLORMAP_VIRIDIS) {
        out->present |= YETTY_YPLOT_OPT_COLORMAP;
        out->colormap = config->colormap;
    }
    if (config->field_min != config->field_max) {
        out->present |= YETTY_YPLOT_OPT_FIELD;
        out->field_min = config->field_min;
        out->field_max = config->field_max;
    }
}

/* Bridge: fold resolved state into the uniforms + effective chrome config.
 * Step 3 replaces this with emit_into consuming `resolved` directly. */
static void yplot_apply_resolved(const struct yetty_yplot_resolved *resolved,
                                 struct yetty_yplot_uniforms *u,
                                 struct yetty_yplot_render_config *config)
{
    if (resolved->has_width) {
        u->bounds_w = resolved->width;
    }
    if (resolved->has_height) {
        u->bounds_h = resolved->height;
    }
    if (resolved->has_x_range) {
        u->x_min = resolved->x_min;
        u->x_max = resolved->x_max;
    }
    if (resolved->has_y_range) {
        u->y_min = resolved->y_min;
        u->y_max = resolved->y_max;
    }
    /* Clear only the toggleable decoration bits so the bytecode-derived
     * USES_TIME / FIELD flags survive. */
    uint32_t flags = u->flags & ~(uint32_t)(YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES |
                                            YETTY_YPLOT_FLAG_LABELS | YETTY_YPLOT_FLAG_XLOG |
                                            YETTY_YPLOT_FLAG_YLOG);
    if (resolved->grid) {
        flags |= YETTY_YPLOT_FLAG_GRID;
    }
    if (resolved->axes) {
        flags |= YETTY_YPLOT_FLAG_AXES;
    }
    if (resolved->labels) {
        flags |= YETTY_YPLOT_FLAG_LABELS;
    }
    if (resolved->x_log) {
        flags |= YETTY_YPLOT_FLAG_XLOG;
    }
    if (resolved->y_log) {
        flags |= YETTY_YPLOT_FLAG_YLOG;
    }
    u->flags = flags;
    u->colormap_id = (uint32_t)resolved->colormap;
    if (resolved->has_field) {
        u->field_min = resolved->field_min;
        u->field_max = resolved->field_max;
    }
    config->title = resolved->title;
    config->x_label = resolved->x_label;
    config->y_label = resolved->y_label;
    config->legend_mode = resolved->legend;
}

struct yetty_ydraw_drawable_list_result yetty_yplot_render(
    const char *source, size_t len, const struct yetty_yplot_render_config *config)
{
    return yetty_yplot_render_with_buffers(source, len, NULL, 0, config);
}

struct yetty_ycore_void_result yetty_yplot_emit_expression(
    const char *source, size_t len, const struct yetty_yplot_buffer_input *buffers,
    size_t buffer_count, const struct yetty_yplot_render_config *config,
    struct yetty_ydraw_drawable_list *dest, float origin_x, float origin_y, float *out_figure_w,
    float *out_figure_h)
{
    if (!dest) {
        return YETTY_ERR(yetty_ycore_void, "yplot: dest is NULL");
    }
    if (!source && len > 0) {
        return YETTY_ERR(yetty_ycore_void, "source is NULL");
    }
    if (!buffers && buffer_count > 0) {
        return YETTY_ERR(yetty_ycore_void, "buffers is NULL but buffer_count > 0");
    }

    struct yetty_yplot_uniforms u;
    uint32_t bc_buf[1024];
    uint32_t bc_len = 0;
    struct yetty_yexpr_arena expr_arena;
    struct yetty_yexpr_plot_expr parsed = {0};

    struct yetty_ycore_void_result ub = yplot_build_uniforms_and_bytecode(
        source, len, config, bc_buf, (uint32_t)(sizeof bc_buf / sizeof bc_buf[0]), &u, &bc_len,
        &expr_arena, &parsed);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ub, "yplot: uniforms/bytecode build failed");

    /* Fold figure/axis attributes from the expression (@plot.title, @plot.size,
     * @x.scale, …) into the uniforms and an effective chrome config. Expression
     * attrs override the caller's config for the fields they name. Nothing has
     * been allocated yet, so an error here returns without cleanup. */
    struct yetty_yplot_render_config effective_config =
        config ? *config : (struct yetty_yplot_render_config){0};
    struct yetty_yplot_options options;
    yplot_options_from_config(config, &options);
    struct yetty_yplot_resolved resolved;
    struct yetty_ycore_void_result resolve_res =
        yetty_yplot_resolve(&parsed, &options, YETTY_YPLOT_EXPRESSION_WINS, &resolved);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, resolve_res, "yplot: bad plot attribute");
    yplot_apply_resolved(&resolved, &u, &effective_config);

    /* Buffer slots come from TWO sources, layered in this order so that
     * sampler-slot indices match what the compiler emitted:
     *   1) declarations from source (`f=buffer; @f.size=N; @f.values=…`)
     *      — slot index == declaration order, matching compiler's LOAD_S idx
     *   2) caller-supplied data buffers via the API — appended after */
    uint32_t decl_count = parsed.buffer_count;
    if (decl_count > 8) {
        decl_count = 8;
    }
    size_t total_bufs = (size_t)decl_count + buffer_count;

    struct yetty_yplot_data_buffer wire_bufs_stack[8];
    struct yetty_yplot_data_buffer *wire_bufs = NULL;
    /* Zero-fill scratch — declarations without inline values render as a
     * flat baseline until their owner streams in real data. Allocated as
     * one slab and aliased to per-decl spans, freed at the end. */
    float *zero_fill = NULL;
    size_t zero_fill_total = 0;

    if (total_bufs > 0) {
        wire_bufs = (total_bufs <= 8) ? wire_bufs_stack : malloc(total_bufs * sizeof(*wire_bufs));
        if (!wire_bufs) {
            return YETTY_ERR(yetty_ycore_void, "yplot: buffer view alloc failed");
        }

        /* First pass: how much zero-fill do we need? */
        for (uint32_t i = 0; i < decl_count; i++) {
            const struct yetty_yexpr_plot_buffer *d = &parsed.buffers[i];
            if (d->inline_count == 0 && d->size > 0) {
                zero_fill_total += d->size;
            }
        }
        if (zero_fill_total > 0) {
            zero_fill = calloc(zero_fill_total, sizeof(float));
            if (!zero_fill) {
                if (wire_bufs != wire_bufs_stack) {
                    free(wire_bufs);
                }
                return YETTY_ERR(yetty_ycore_void, "yplot: zero-fill alloc failed");
            }
        }

        /* Second pass: populate wire entries for declarations. */
        size_t zf_off = 0;
        for (uint32_t i = 0; i < decl_count; i++) {
            const struct yetty_yexpr_plot_buffer *d = &parsed.buffers[i];
            if (d->inline_count > 0) {
                wire_bufs[i].samples = d->inline_values;
                wire_bufs[i].count = d->inline_count;
            } else if (d->size > 0) {
                wire_bufs[i].samples = zero_fill + zf_off;
                wire_bufs[i].count = d->size;
                zf_off += d->size;
            } else {
                /* No values, no size — render a degenerate two-zero buffer
                 * so the shader's >=2 check is satisfied and the curve is
                 * a flat baseline until the owner sets it up. */
                wire_bufs[i].samples = NULL;
                wire_bufs[i].count = 0;
            }
        }

        /* Caller-supplied buffers append after declarations. */
        for (size_t i = 0; i < buffer_count; i++) {
            wire_bufs[decl_count + i].samples = buffers[i].samples;
            wire_bufs[decl_count + i].count = buffers[i].count;
        }
    }

    /* Per-buffer color slots: expressions occupy 0..function_count-1, then
     * buffers (declarations first, then API) fill the next slots (mod 8).
     * Caller-supplied colors override the palette defaults already filled.
     * Uncertainty envelopes and hidden band inputs map to the band_slots /
     * hidden_mask uniforms (buffer-array indices → data-buffer slots). */
    for (size_t i = 0; i < buffer_count; i++) {
        uint32_t slot = (u.function_count + (uint32_t)decl_count + (uint32_t)i) % 8u;
        if (buffers[i].color != 0u) {
            u.colors[slot] = buffers[i].color;
        }
        if (buffers[i].has_band && buffers[i].band_lo >= 0 && buffers[i].band_hi >= 0 &&
            (size_t)buffers[i].band_lo < buffer_count &&
            (size_t)buffers[i].band_hi < buffer_count) {
            uint32_t lo_slot = (uint32_t)decl_count + (uint32_t)buffers[i].band_lo;
            uint32_t hi_slot = (uint32_t)decl_count + (uint32_t)buffers[i].band_hi;
            if (lo_slot < 8u && hi_slot < 8u) {
                u.band_slots[slot] = (lo_slot + 1u) | ((hi_slot + 1u) << 8) |
                                     ((uint32_t)buffers[i].band_style << 16);
            }
        }
        if (buffers[i].hidden) {
            uint32_t data_slot = (uint32_t)decl_count + (uint32_t)i;
            if (data_slot < 32u) {
                u.hidden_mask |= 1u << data_slot;
            }
        }
        if (buffers[i].ring) {
            uint32_t data_slot = (uint32_t)decl_count + (uint32_t)i;
            if (data_slot < 8u) {
                u.ring_heads[data_slot] = 1u; /* ring on, head at index 0 */
            }
        }
    }

    struct yetty_yplot_buffers bufs = {
        .bytecode = bc_len > 0 ? bc_buf : NULL,
        .bytecode_len = bc_len,
        .data = wire_bufs,
        .data_count = total_bufs,
    };

    /* Legend: one entry per named expression curve plus one per named API
     * data buffer, each in its plot color. Expression names point into
     * `parsed`, which outlives the emit call below. */
    struct yplot_legend_entry legend[YETTY_YEXPR_MAX_PLOT_DEFS + 8];
    size_t legend_count = 0;
    for (uint32_t i = 0; i < u.function_count && legend_count < YETTY_YEXPR_MAX_PLOT_DEFS; i++) {
        legend[legend_count].name = parsed.defs[i].name;
        legend[legend_count].color = u.colors[i];
        legend_count++;
    }
    for (size_t i = 0; i < buffer_count && legend_count < YETTY_YEXPR_MAX_PLOT_DEFS + 8; i++) {
        if (!buffers[i].name || !buffers[i].name[0] || buffers[i].hidden) {
            continue;
        }
        uint32_t slot = (u.function_count + (uint32_t)decl_count + (uint32_t)i) % 8u;
        legend[legend_count].name = buffers[i].name;
        legend[legend_count].color = u.colors[slot];
        legend_count++;
    }

    /* Assemble the render plan and emit it through the one shared path into a
     * fresh list. The list spans the full figure extents so the reserved label
     * margins are not clipped. */
    struct yetty_yplot_render_plan render_plan = {0};
    render_plan.uniforms = u;
    render_plan.buffers = bufs;
    render_plan.legend_count = legend_count;
    for (size_t i = 0; i < legend_count; i++) {
        render_plan.legend_names[i] = legend[i].name;
        render_plan.legend_colors[i] = legend[i].color;
    }
    render_plan.title = effective_config.title;
    render_plan.x_label = effective_config.x_label;
    render_plan.y_label = effective_config.y_label;
    render_plan.legend_mode = effective_config.legend_mode;

    /* The plan's data buffers alias `wire_bufs`/`zero_fill`, so they must stay
     * alive across the emit; free them only after it returns. */
    struct yetty_ycore_void_result emit_res =
        yetty_yplot_emit_into(&render_plan, dest, origin_x, origin_y);
    if (wire_bufs && wire_bufs != wire_bufs_stack) {
        free(wire_bufs);
    }
    free(zero_fill);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_res, "yplot: emit");

    if (out_figure_w) {
        *out_figure_w = render_plan.uniforms.bounds_w;
    }
    if (out_figure_h) {
        *out_figure_h = render_plan.uniforms.bounds_h;
    }
    return YETTY_OK_VOID();
}

struct yetty_ydraw_drawable_list_result yetty_yplot_render_with_buffers(
    const char *source, size_t len, const struct yetty_yplot_buffer_input *buffers,
    size_t buffer_count, const struct yetty_yplot_render_config *config)
{
    /* Standalone render: emit the expression into a fresh list at the config
     * origin, then set the list's scene bounds to the full figure extents (the
     * reserved label margins live inside these bounds, so nothing is clipped). */
    struct yetty_ydraw_drawable_list_result list_res =
        yetty_ydraw_drawable_list_config_buffer_create(NULL);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, list_res, "yplot: list create");

    float origin_x = config ? config->bounds_x : 0.0f;
    float origin_y = config ? config->bounds_y : 0.0f;
    float figure_w = 0.0f, figure_h = 0.0f;
    struct yetty_ycore_void_result emit_res =
        yetty_yplot_emit_expression(source, len, buffers, buffer_count, config, list_res.value,
                                    origin_x, origin_y, &figure_w, &figure_h);
    if (YETTY_IS_ERR(emit_res)) {
        yetty_ydraw_drawable_list_destroy(list_res.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: emit expression", emit_res);
    }
    yetty_ydraw_drawable_list_set_scene_bounds(list_res.value, 0.0f, 0.0f, origin_x + figure_w,
                                               origin_y + figure_h);
    return YETTY_OK(yetty_ydraw_drawable_list, list_res.value);
}

struct yetty_ydraw_drawable_list_result yetty_yplot_render_program(
    const uint32_t *program, uint32_t program_words, const struct yetty_yplot_render_config *config)
{
    if (!program) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: program is NULL");
    }
    /* Fully validate the serialized word layout before trusting any field —
     * this blob may have arrived from an external frontend and is headed for
     * the GPU interpreter, where malformed bytecode is undefined behaviour. */
    struct yetty_ycore_void_result valid = yetty_yfsvm_validate_serialized(program, program_words);
    if (YETTY_IS_ERR(valid)) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: invalid program bytecode", valid);
    }

    struct yetty_yplot_uniforms u;
    yplot_init_base_uniforms(config, &u);

    uint32_t function_count = program[2];
    uint32_t const_count = program[3];
    u.function_count = function_count > 8u ? 8u : function_count;

    /* Derive the animation/field flags by scanning the code segment for the
     * input opcodes — the same signals the expression path reads from the
     * compiler's uses_time / uses_y. The code segment follows the padded
     * function table and the constant pool. */
    uint32_t code_offset = 4u + YFSVM_MAX_FUNCTIONS + const_count;
    if (code_offset <= program_words) {
        for (uint32_t i = code_offset; i < program_words; i++) {
            uint32_t op = yfsvm_decode_opcode(program[i]);
            if (op == YETTY_YFSVM_OP_LOAD_T) {
                u.flags |= YETTY_YPLOT_FLAG_USES_TIME;
            } else if (op == YETTY_YFSVM_OP_LOAD_Y) {
                u.flags |= YETTY_YPLOT_FLAG_FIELD;
            }
        }
    }

    struct yetty_yplot_buffers bufs = {
        .bytecode = program,
        .bytecode_len = program_words,
        .data = NULL,
        .data_count = 0,
    };

    /* Config-only path (no expression attributes to resolve): build the plan
     * straight from the caller's config. Precompiled programs carry no
     * per-curve names, so no legend. */
    struct yetty_yplot_render_plan render_plan = {0};
    render_plan.uniforms = u;
    render_plan.buffers = bufs;
    render_plan.legend_count = 0;
    if (config) {
        render_plan.title = config->title;
        render_plan.x_label = config->x_label;
        render_plan.y_label = config->y_label;
        render_plan.legend_mode = config->legend_mode;
    }
    return yplot_emit_to_new_list(&render_plan);
}

struct yetty_ycore_size_result yetty_yplot_dcs_bin_emit(
    const struct yetty_ydraw_drawable_list *buffer, FILE *out)
{
    if (!buffer || !out) {
        return YETTY_ERR(yetty_ycore_size, "yplot_dcs_bin_emit: NULL buffer or out");
    }
    const uint8_t *raw = NULL;
    size_t raw_size =
        yetty_ydraw_drawable_list_serialize((struct yetty_ydraw_drawable_list *)buffer, &raw);
    if (raw_size == 0 || !raw) {
        return YETTY_ERR(yetty_ycore_size, "yplot_dcs_bin_emit: empty serialize");
    }

    struct yetty_yface_bin_meta meta = {
        .magic = YETTY_YFACE_BIN_MAGIC,
        .version = YETTY_YFACE_BIN_VERSION,
        .compressed = YETTY_YFACE_COMP_LZ4F,
        .compression_algo = 0,
        .raw_size = raw_size,
        .reserved = {0, 0},
    };
    struct yetty_ycore_buffer envelope = {0};
    struct yetty_ycore_void_result r = yetty_yface_emit(
        YETTY_DCS_YDRAW_BIN, /*compressed=*/1, &meta, sizeof(meta), raw, raw_size, &envelope);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_buffer_destroy(&envelope);
        return YETTY_ERR(yetty_ycore_size, "yplot_dcs_bin_emit: yface_emit failed", r);
    }

    size_t written = 0;
    if (envelope.size > 0) {
        written = fwrite(envelope.data, 1, envelope.size, out);
    }
    yetty_ycore_buffer_destroy(&envelope);
    return YETTY_OK(yetty_ycore_size, written);
}
