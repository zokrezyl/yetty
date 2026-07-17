/*
 * yplot.c — high-level convenience wrappers around the auto-generated
 * yplot-gen.c API. See include/yetty/yplot/yplot.h for the contract.
 */

#include <yetty/yplot/yplot.h>

#include <yetty/yexpr/yexpr.h>
#include <yetty/yfsvm/compiler.h>
#include <yetty/yface/yface.h>
#include <yetty/ydraw-core/drawable-list.h>
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
    u->flags = config && config->flags
                   ? config->flags
                   : (YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS);

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

    /* Domain / viewport overrides come from inline `x=A..B` etc. The
     * @view= viewport currently rebinds the static domain — the shader
     * doesn't yet animate zoom from this, but the override still gives
     * a useful initial framing for the first frame. */
    if (pr.value.has_x_range) {
        u->x_min = pr.value.x_min;
        u->x_max = pr.value.x_max;
    }
    if (pr.value.has_y_range) {
        u->y_min = pr.value.y_min;
        u->y_max = pr.value.y_max;
    }
    if (pr.value.has_view) {
        u->x_min = pr.value.view_x_min;
        u->x_max = pr.value.view_x_max;
        u->y_min = pr.value.view_y_min;
        u->y_max = pr.value.view_y_max;
    }

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
static struct yetty_ydraw_drawable_list_result yplot_emit_prim(
    const struct yetty_yplot_uniforms *u_in, const struct yetty_yplot_buffers *bufs,
    const struct yplot_legend_entry *legend_entries, size_t legend_count,
    const struct yetty_yplot_render_config *config)
{
    struct yetty_yplot_uniforms u = *u_in;

    /* A log axis over a non-positive range has no defined mapping — reject
     * it here, the shared choke point of every render entry path. */
    if ((u.flags & YETTY_YPLOT_FLAG_XLOG) && !(u.x_min > 0.0f && u.x_max > u.x_min)) {
        return YETTY_ERR(yetty_ydraw_drawable_list,
                         "yplot: x log scale requires 0 < x_min < x_max");
    }
    if ((u.flags & YETTY_YPLOT_FLAG_YLOG) && !(u.y_min > 0.0f && u.y_max > u.y_min)) {
        return YETTY_ERR(yetty_ydraw_drawable_list,
                         "yplot: y log scale requires 0 < y_min < y_max");
    }

    /* Full figure extents BEFORE any label inset — the drawable list scene
     * must span these so the bottom/left labels in the reserved margins are
     * not clipped and the scrollback figure reserves enough rows. */
    float figure_x = u.bounds_x;
    float figure_y = u.bounds_y;
    float figure_w = u.bounds_w;
    float figure_h = u.bounds_h;
    float figure_max_x = u.bounds_x + u.bounds_w;
    float figure_max_y = u.bounds_y + u.bounds_h;

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
        return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: prim alloc failed");
    }
    struct yetty_ycore_size_result ser =
        yetty_yplot_uniforms_serialize(&u, bufs, drawable_buf, required);
    if (YETTY_IS_ERR(ser)) {
        free(drawable_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: serialize failed", ser);
    }

    struct yetty_ydraw_drawable_list_config bcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = figure_max_x,
        .scene_max_y = figure_max_y,
    };
    struct yetty_ydraw_drawable_list_result br =
        yetty_ydraw_drawable_list_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(br)) {
        free(drawable_buf);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: ydraw buffer create failed", br);
    }

    struct yetty_ydraw_id_result idr =
        yetty_ydraw_drawable_list_add_prim(br.value, drawable_buf, required);
    free(drawable_buf);
    if (YETTY_IS_ERR(idr)) {
        yetty_ydraw_drawable_list_destroy(br.value);
        return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: ydraw add_prim failed", idr);
    }

    if (plan.enabled) {
        struct yetty_ycore_void_result label_res =
            yplot_emit_axis_labels(br.value, &u, &plan, figure_max_x);
        if (YETTY_IS_ERR(label_res)) {
            yetty_ydraw_drawable_list_destroy(br.value);
            return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: axis labels", label_res);
        }
        if (plan.have_title || plan.have_x_label || plan.have_y_label) {
            struct yetty_ycore_void_result chrome_res = yplot_emit_chrome(
                br.value, &u, &plan, figure_x, figure_y, figure_w, figure_h, config);
            if (YETTY_IS_ERR(chrome_res)) {
                yetty_ydraw_drawable_list_destroy(br.value);
                return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: figure chrome", chrome_res);
            }
        }
    }
    if (show_legend) {
        /* The strip's left edge is the inset plot's right edge. */
        float margin_left = u.bounds_x + u.bounds_w;
        struct yetty_ycore_void_result legend_res =
            yplot_emit_legend(br.value, &u, &plan, margin_left, legend_entries, legend_count);
        if (YETTY_IS_ERR(legend_res)) {
            yetty_ydraw_drawable_list_destroy(br.value);
            return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: legend", legend_res);
        }
    }
    if (show_colorbar) {
        float margin_left = u.bounds_x + u.bounds_w;
        struct yetty_ycore_void_result colorbar_res =
            yplot_emit_colorbar(br.value, &u, &plan, margin_left);
        if (YETTY_IS_ERR(colorbar_res)) {
            yetty_ydraw_drawable_list_destroy(br.value);
            return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: colorbar", colorbar_res);
        }
    }
    return YETTY_OK(yetty_ydraw_drawable_list, br.value);
}

struct yetty_ydraw_drawable_list_result yetty_yplot_render(
    const char *source, size_t len, const struct yetty_yplot_render_config *config)
{
    return yetty_yplot_render_with_buffers(source, len, NULL, 0, config);
}

struct yetty_ydraw_drawable_list_result yetty_yplot_render_with_buffers(
    const char *source, size_t len, const struct yetty_yplot_buffer_input *buffers,
    size_t buffer_count, const struct yetty_yplot_render_config *config)
{
    if (!source && len > 0) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "source is NULL");
    }
    if (!buffers && buffer_count > 0) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "buffers is NULL but buffer_count > 0");
    }

    struct yetty_yplot_uniforms u;
    uint32_t bc_buf[1024];
    uint32_t bc_len = 0;
    struct yetty_yexpr_arena expr_arena;
    struct yetty_yexpr_plot_expr parsed = {0};

    struct yetty_ycore_void_result ub = yplot_build_uniforms_and_bytecode(
        source, len, config, bc_buf, (uint32_t)(sizeof bc_buf / sizeof bc_buf[0]), &u, &bc_len,
        &expr_arena, &parsed);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, ub, "yplot: uniforms/bytecode build failed");

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
            return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: buffer view alloc failed");
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
                return YETTY_ERR(yetty_ydraw_drawable_list, "yplot: zero-fill alloc failed");
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

    struct yetty_ydraw_drawable_list_result out =
        yplot_emit_prim(&u, &bufs, legend, legend_count, config);
    if (wire_bufs && wire_bufs != wire_bufs_stack) {
        free(wire_bufs);
    }
    free(zero_fill);
    return out;
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
    /* Precompiled programs carry no per-curve names, so no legend. */
    return yplot_emit_prim(&u, &bufs, NULL, 0, config);
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
