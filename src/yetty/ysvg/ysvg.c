/*
 * ysvg.c — public entry point (yetty_ysvg_render).
 *
 * Pipeline:
 *   1. Parse args ("--font-size", "--line-spacing", "--bg").
 *   2. Parse SVG XML → struct yetty_ysvg_doc tree (ysvg-parse.c).
 *   3. Resolve scene bounds from <svg> width/height/viewBox or config.
 *   4. Create the ydraw buffer pre-configured with those bounds.
 *   5. Optional background fill.
 *   6. Walk the tree emitting ydraw primitives (ysvg-paint.c).
 *
 * Mirrors ymarkdown/ypdf shape — caller owns the returned buffer.
 */

#include "ysvg-internal.h"

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ycore/types.h>
#include <yetty/ysvg/ysvg.h>
#include <yetty/ytrace/ytrace.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define YSVG_DEFAULT_LINE_SPACING 1.2f

struct ysvg_params {
    float font_size;
    float line_spacing;
    int has_font_size;
    int has_bg;
    uint32_t bg_color; /* RGBA, 0 if absent */
};

static int ysvg_params_starts_with(const char *s, size_t s_len, const char *prefix)
{
    size_t pl = strlen(prefix);
    if (s_len < pl) return 0;
    return memcmp(s, prefix, pl) == 0;
}

static int ysvg_params_parse_float(const char *s, size_t len, float *out)
{
    char buf[64];
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, s, len);
    buf[len] = '\0';
    char *end = NULL;
    float v = strtof(buf, &end);
    if (end == buf) return 0;
    *out = v;
    return 1;
}

static void ysvg_params_parse(const char *args, size_t args_len, struct ysvg_params *p)
{
    if (!args || args_len == 0) return;
    size_t i = 0;
    while (i < args_len) {
        while (i < args_len && (args[i] == ' ' || args[i] == '\t' || args[i] == '\n' ||
                                args[i] == '\r'))
            i++;
        if (i >= args_len) break;
        size_t s = i;
        while (i < args_len && args[i] != ' ' && args[i] != '\t' && args[i] != '\n' &&
               args[i] != '\r')
            i++;
        size_t tlen = i - s;
        const char *tok = args + s;
        if (ysvg_params_starts_with(tok, tlen, "--font-size=")) {
            float v;
            if (ysvg_params_parse_float(tok + 12, tlen - 12, &v)) {
                p->font_size = v;
                p->has_font_size = 1;
            }
        } else if (ysvg_params_starts_with(tok, tlen, "--line-spacing=")) {
            float v;
            if (ysvg_params_parse_float(tok + 15, tlen - 15, &v)) p->line_spacing = v;
        } else if (ysvg_params_starts_with(tok, tlen, "--bg=")) {
            uint32_t c;
            if (yetty_ysvg_parse_color(tok + 5, tlen - 5, &c)) {
                p->bg_color = c;
                p->has_bg = 1;
            }
        }
    }
}

/*=============================================================================
 * Scene-bounds resolution
 *===========================================================================*/

/* Resolve the SVG's user-space coordinate system from <svg viewBox=…>
 * (preferred) or width/height attrs. Returns origin (vx, vy) and size
 * (vw, vh) in user units. */
static void resolve_user_space(const struct yetty_ysvg_doc *doc, float *vx, float *vy, float *vw,
                               float *vh)
{
    *vx = 0.0f;
    *vy = 0.0f;
    *vw = 0.0f;
    *vh = 0.0f;
    const struct yetty_ysvg_node *root = doc->root;
    if (!root) return;

    const struct yetty_ysvg_attr *vb = yetty_ysvg_attr_find(root, YETTY_YSVG_ATTR_VIEWBOX);
    if (vb) {
        size_t p = 0;
        float a, b, c, d;
        if (yetty_ysvg_parse_number(vb->value, vb->value_len, &p, &a) &&
            yetty_ysvg_parse_number(vb->value, vb->value_len, &p, &b) &&
            yetty_ysvg_parse_number(vb->value, vb->value_len, &p, &c) &&
            yetty_ysvg_parse_number(vb->value, vb->value_len, &p, &d) && c > 0.0f && d > 0.0f) {
            *vx = a;
            *vy = b;
            *vw = c;
            *vh = d;
            return;
        }
    }
    const struct yetty_ysvg_attr *wa = yetty_ysvg_attr_find(root, YETTY_YSVG_ATTR_WIDTH);
    const struct yetty_ysvg_attr *ha = yetty_ysvg_attr_find(root, YETTY_YSVG_ATTR_HEIGHT);
    if (wa) *vw = yetty_ysvg_parse_length(wa->value, wa->value_len, 100.0f, 0.0f);
    if (ha) *vh = yetty_ysvg_parse_length(ha->value, ha->value_len, 100.0f, 0.0f);
}

/* Compute the display-pixel target size for an SVG of user-space size
 * (vw × vh) under the given render config. We pick a width-fit policy:
 * target_w = full terminal width (in pixels); target_h follows the
 * viewBox aspect ratio. */
static void resolve_target_size(const struct yetty_ysvg_render_config *cfg, float vw, float vh,
                                float *target_w, float *target_h)
{
    float cell_w = (cfg && cfg->cell_width > 0) ? (float)cfg->cell_width : 8.0f;
    float cell_h = (cfg && cfg->cell_height > 0) ? (float)cfg->cell_height : 16.0f;
    uint32_t cols = (cfg && cfg->width_cells > 0) ? cfg->width_cells : 80;
    float tw = (float)cols * cell_w;
    if (vw <= 0.0f || vh <= 0.0f) {
        *target_w = tw;
        *target_h = (cfg && cfg->height_cells > 0) ? (float)cfg->height_cells * cell_h : tw;
        return;
    }
    float th = tw * vh / vw;
    /* If a max height is requested, cap to it and shrink width proportionally. */
    if (cfg && cfg->height_cells > 0) {
        float th_max = (float)cfg->height_cells * cell_h;
        if (th > th_max) {
            th = th_max;
            tw = th * vw / vh;
        }
    }
    *target_w = tw;
    *target_h = th;
}

/*=============================================================================
 * Public entry point
 *===========================================================================*/

struct yetty_ysvg_render_result yetty_ysvg_render(const char *content, size_t content_len,
                                                  const char *args, size_t args_len,
                                                  const struct yetty_ysvg_render_config *config)
{
    if (!content && content_len > 0) {
        return YETTY_ERR(yetty_ysvg_render, "ysvg: content is NULL but content_len > 0");
    }
    struct ysvg_params params = {0};
    params.font_size = (config && config->cell_height > 0) ? (float)config->cell_height : 14.0f;
    params.line_spacing = YSVG_DEFAULT_LINE_SPACING;
    /* SVG content is authored for a white page; yetty terminals are dark.
     * Instead of forcing a white BG (which fights the terminal palette), the
     * paint pass lightness-inverts every resolved colour so the document
     * reads correctly on black. BG stays transparent by default. Override
     * with `--bg=#RRGGBB`. */
    ysvg_params_parse(args, args_len, &params);

    struct yetty_ysvg_doc_ptr_result pr = yetty_ysvg_parse(content, content_len);
    if (YETTY_IS_ERR(pr)) {
        return YETTY_ERR(yetty_ysvg_render, "ysvg: parse failed", pr);
    }
    struct yetty_ysvg_doc *doc = pr.value;

    /* Map SVG user space (viewBox) → display pixel space. The yetty
     * canvas treats incoming primitive coords as pixels, so we bake
     * the scaling into a root transform applied by the paint pass. */
    float vx = 0, vy = 0, vw = 0, vh = 0;
    resolve_user_space(doc, &vx, &vy, &vw, &vh);
    float scene_w = 0.0f, scene_h = 0.0f;
    resolve_target_size(config, vw, vh, &scene_w, &scene_h);
    float scale = (vw > 0.0f) ? scene_w / vw : 1.0f;

    struct yetty_ydraw_draw_list_config bcfg = {.scene_min_x = 0.0f,
                                                   .scene_min_y = 0.0f,
                                                   .scene_max_x = scene_w,
                                                   .scene_max_y = scene_h};
    struct yetty_ydraw_draw_list_result br =
        yetty_ydraw_draw_list_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(br)) {
        yetty_ysvg_doc_destroy(doc);
        return YETTY_ERR(yetty_ysvg_render, "ysvg: buffer create failed", br);
    }
    struct yetty_ydraw_draw_list *buf = br.value;

    /* Optional background — emit before any other primitives. */
    if (params.has_bg && (params.bg_color & 0xFFu) != 0) {
        uint32_t abgr = yetty_ysvg_rgba_to_abgr(params.bg_color);
        struct yetty_ysdf_box bg = {.center_x = scene_w * 0.5f,
                                    .center_y = scene_h * 0.5f,
                                    .half_width = scene_w * 0.5f,
                                    .half_height = scene_h * 0.5f,
                                    .corner_radius = 0.0f};
        struct yetty_ycore_void_result r = yetty_ydraw_draw_list_add_cmd_add_box(buf, 0, 0, abgr, 0, 0.0f, &bg);
        if (YETTY_IS_ERR(r)) {
            yetty_ydraw_draw_list_destroy(buf);
            yetty_ysvg_doc_destroy(doc);
            return YETTY_ERR(yetty_ysvg_render, "ysvg: background emit failed", r);
        }
    }

    struct yetty_ysvg_paint_ctx ctx = {.buf = buf,
                                       .default_font_size = params.font_size,
                                       .line_spacing = params.line_spacing,
                                       .scene_min_x = 0.0f,
                                       .scene_min_y = 0.0f,
                                       .scene_max_x = scene_w,
                                       .scene_max_y = scene_h,
                                       .user_to_pixel_scale = scale};
    /* root_ctm = scale(scale) · translate(-vx, -vy)  — maps (x, y) in
     * SVG user space to (scale·(x-vx), scale·(y-vy)) in pixels. */
    ctx.root_ctm.a = scale;
    ctx.root_ctm.b = 0.0f;
    ctx.root_ctm.c = 0.0f;
    ctx.root_ctm.d = scale;
    ctx.root_ctm.e = -vx * scale;
    ctx.root_ctm.f = -vy * scale;
    struct yetty_ycore_void_result er = yetty_ysvg_paint(doc, &ctx);
    yetty_ysvg_doc_destroy(doc);
    if (YETTY_IS_ERR(er)) {
        yetty_ydraw_draw_list_destroy(buf);
        return YETTY_ERR(yetty_ysvg_render, "ysvg: emission failed", er);
    }

    struct yetty_ysvg_render_output out = {
        .buffer = buf, .scene_width = scene_w, .scene_height = scene_h};
    return YETTY_OK(yetty_ysvg_render, out);
}
