/*
 * ylottie.c — public entry point (yetty_ylottie_render).
 *
 * Pipeline:
 *   1. Parse args ("--frame", "--time", "--bg").
 *   2. Parse JSON → struct yetty_ylottie_doc (ylottie-json.c).
 *   3. Resolve the frame to render (default = composition in-point `ip`).
 *   4. Resolve scene bounds from the composition w/h (width-fit policy).
 *   5. Create the ydraw buffer pre-configured with those bounds.
 *   6. Optional background fill.
 *   7. Walk the layers emitting ydraw primitives (ylottie-paint.c).
 *
 * Mirrors the ysvg / ymarkdown / ypdf shape — the caller owns the buffer.
 */

#include "ylottie-internal.h"

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ylottie/ylottie.h>
#include <yetty/ysdf/funcs.gen.h>
#include <yetty/ysdf/types.gen.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct ylottie_params {
    float frame;
    int has_frame;
    float time_seconds;
    int has_time;
    int has_bg;
    uint32_t bg_abgr;
};

static int params_starts_with(const char *s, size_t s_len, const char *prefix)
{
    size_t pl = strlen(prefix);
    if (s_len < pl) {
        return 0;
    }
    return memcmp(s, prefix, pl) == 0;
}

static int params_parse_float(const char *s, size_t len, float *out)
{
    char buf[64];
    if (len >= sizeof(buf)) {
        len = sizeof(buf) - 1;
    }
    memcpy(buf, s, len);
    buf[len] = '\0';
    char *end = NULL;
    float v = strtof(buf, &end);
    if (end == buf) {
        return 0;
    }
    *out = v;
    return 1;
}

static int hex_nibble(char c, uint32_t *out)
{
    if (c >= '0' && c <= '9') {
        *out = (uint32_t)(c - '0');
    } else if (c >= 'a' && c <= 'f') {
        *out = (uint32_t)(c - 'a' + 10);
    } else if (c >= 'A' && c <= 'F') {
        *out = (uint32_t)(c - 'A' + 10);
    } else {
        return 0;
    }
    return 1;
}

/* Parse #RGB / #RRGGBB / #RRGGBBAA into an ABGR word. Returns 1 on success. */
int yetty_ylottie_parse_color(const char *s, size_t len, uint32_t *out_abgr)
{
    if (len > 0 && s[0] == '#') {
        s++;
        len--;
    }
    uint32_t v[8];
    if (len != 3 && len != 6 && len != 8) {
        return 0;
    }
    for (size_t i = 0; i < len; i++) {
        if (!hex_nibble(s[i], &v[i])) {
            return 0;
        }
    }
    float r, g, b, a = 1.0f;
    if (len == 3) {
        r = (float)((v[0] << 4) | v[0]) / 255.0f;
        g = (float)((v[1] << 4) | v[1]) / 255.0f;
        b = (float)((v[2] << 4) | v[2]) / 255.0f;
    } else {
        r = (float)((v[0] << 4) | v[1]) / 255.0f;
        g = (float)((v[2] << 4) | v[3]) / 255.0f;
        b = (float)((v[4] << 4) | v[5]) / 255.0f;
        if (len == 8) {
            a = (float)((v[6] << 4) | v[7]) / 255.0f;
        }
    }
    *out_abgr = yetty_ylottie_rgba_to_abgr(r, g, b, a);
    return 1;
}

static void params_parse(const char *args, size_t args_len, struct ylottie_params *p)
{
    if (!args || args_len == 0) {
        return;
    }
    size_t i = 0;
    while (i < args_len) {
        while (i < args_len &&
               (args[i] == ' ' || args[i] == '\t' || args[i] == '\n' || args[i] == '\r')) {
            i++;
        }
        if (i >= args_len) {
            break;
        }
        size_t start = i;
        while (i < args_len && args[i] != ' ' && args[i] != '\t' && args[i] != '\n' &&
               args[i] != '\r') {
            i++;
        }
        size_t tlen = i - start;
        const char *tok = args + start;
        if (params_starts_with(tok, tlen, "--frame=")) {
            float v;
            if (params_parse_float(tok + 8, tlen - 8, &v)) {
                p->frame = v;
                p->has_frame = 1;
            }
        } else if (params_starts_with(tok, tlen, "--time=")) {
            float v;
            if (params_parse_float(tok + 7, tlen - 7, &v)) {
                p->time_seconds = v;
                p->has_time = 1;
            }
        } else if (params_starts_with(tok, tlen, "--bg=")) {
            uint32_t c;
            if (yetty_ylottie_parse_color(tok + 5, tlen - 5, &c)) {
                p->bg_abgr = c;
                p->has_bg = 1;
            }
        }
    }
}

/*=============================================================================
 * Content sniff
 *===========================================================================*/

static int mem_contains(const char *hay, size_t hlen, const char *needle)
{
    size_t nlen = strlen(needle);
    if (nlen == 0 || hlen < nlen) {
        return 0;
    }
    for (size_t i = 0; i + nlen <= hlen; i++) {
        if (memcmp(hay + i, needle, nlen) == 0) {
            return 1;
        }
    }
    return 0;
}

int yetty_ylottie_can_parse(const char *content, size_t len)
{
    if (!content || len < 8) {
        return 0;
    }
    size_t i = 0;
    while (i < len &&
           (content[i] == ' ' || content[i] == '\t' || content[i] == '\n' || content[i] == '\r')) {
        i++;
    }
    if (i >= len || content[i] != '{') {
        return 0;
    }
    const char *p = content + i;
    size_t avail = len - i;
    size_t scan = avail < 8192 ? avail : 8192;
    if (!mem_contains(p, scan, "\"layers\"")) {
        return 0;
    }
    return mem_contains(p, scan, "\"fr\"") || mem_contains(p, scan, "\"ip\"") ||
           mem_contains(p, scan, "\"op\"");
}

struct yetty_ylottie_info_result yetty_ylottie_inspect(const char *content, size_t len)
{
    struct yetty_ylottie_doc_ptr_result pr = yetty_ylottie_json_parse(content, len);
    if (YETTY_IS_ERR(pr)) {
        return YETTY_ERR(yetty_ylottie_info, "ylottie: JSON parse failed", pr);
    }
    struct yetty_ylottie_doc *doc = pr.value;
    const struct yetty_ylottie_json *root = doc->root;
    if (!root || root->type != YETTY_YLOTTIE_JSON_OBJECT) {
        yetty_ylottie_doc_destroy(doc);
        return YETTY_ERR(yetty_ylottie_info, "ylottie: root is not a JSON object");
    }
    struct yetty_ylottie_info info = {0};
    info.frame_rate = (float)yetty_ylottie_json_num_key(root, "fr", 30.0);
    info.in_point = (float)yetty_ylottie_json_num_key(root, "ip", 0.0);
    info.out_point = (float)yetty_ylottie_json_num_key(root, "op", 0.0);
    info.width = (float)yetty_ylottie_json_num_key(root, "w", 0.0);
    info.height = (float)yetty_ylottie_json_num_key(root, "h", 0.0);
    const struct yetty_ylottie_json *layers = yetty_ylottie_json_get(root, "layers");
    info.layer_count =
        (layers && layers->type == YETTY_YLOTTIE_JSON_ARRAY) ? (int)layers->child_count : 0;
    yetty_ylottie_doc_destroy(doc);
    return YETTY_OK(yetty_ylottie_info, info);
}

/*=============================================================================
 * Scene-bounds resolution (width-fit, same policy as ysvg)
 *===========================================================================*/

static void resolve_target_size(const struct yetty_ylottie_render_config *cfg, float vw, float vh,
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
 * Animation handle — parse once, render many frames
 *===========================================================================*/

struct yetty_ylottie_animation {
    struct yetty_ylottie_doc *doc;           /* owns the parsed JSON */
    const struct yetty_ylottie_json *layers; /* borrowed: root["layers"] */
    struct yetty_ylottie_info info;
    float scene_w;
    float scene_h;
    float default_font_size;
    struct yetty_ylottie_xform root_ctm; /* composition-unit → pixel */
};

struct yetty_ylottie_animation_ptr_result yetty_ylottie_animation_create(
    const char *content, size_t content_len, const struct yetty_ylottie_render_config *config)
{
    if (!content && content_len > 0) {
        return YETTY_ERR(yetty_ylottie_animation_ptr,
                         "ylottie: content is NULL but content_len > 0");
    }
    struct yetty_ylottie_doc_ptr_result pr = yetty_ylottie_json_parse(content, content_len);
    if (YETTY_IS_ERR(pr)) {
        return YETTY_ERR(yetty_ylottie_animation_ptr, "ylottie: JSON parse failed", pr);
    }
    struct yetty_ylottie_doc *doc = pr.value;
    const struct yetty_ylottie_json *root = doc->root;
    if (!root || root->type != YETTY_YLOTTIE_JSON_OBJECT) {
        yetty_ylottie_doc_destroy(doc);
        return YETTY_ERR(yetty_ylottie_animation_ptr, "ylottie: root is not a JSON object");
    }

    struct yetty_ylottie_animation *anim = calloc(1, sizeof(*anim));
    if (!anim) {
        yetty_ylottie_doc_destroy(doc);
        return YETTY_ERR(yetty_ylottie_animation_ptr, "ylottie: out of memory");
    }
    anim->doc = doc;
    anim->layers = yetty_ylottie_json_get(root, "layers");
    anim->info.frame_rate = (float)yetty_ylottie_json_num_key(root, "fr", 30.0);
    anim->info.in_point = (float)yetty_ylottie_json_num_key(root, "ip", 0.0);
    anim->info.out_point = (float)yetty_ylottie_json_num_key(root, "op", 0.0);
    anim->info.width = (float)yetty_ylottie_json_num_key(root, "w", 0.0);
    anim->info.height = (float)yetty_ylottie_json_num_key(root, "h", 0.0);
    anim->info.layer_count = (anim->layers && anim->layers->type == YETTY_YLOTTIE_JSON_ARRAY)
                                 ? (int)anim->layers->child_count
                                 : 0;

    resolve_target_size(config, anim->info.width, anim->info.height, &anim->scene_w,
                        &anim->scene_h);
    float scale = (anim->info.width > 0.0f) ? anim->scene_w / anim->info.width : 1.0f;
    anim->default_font_size =
        (config && config->cell_height > 0) ? (float)config->cell_height : 14.0f;
    /* root_ctm: uniform composition-unit → pixel scale (Lottie's origin is
     * already top-left, y-down, like the ydraw canvas). */
    anim->root_ctm.a = scale;
    anim->root_ctm.b = 0.0f;
    anim->root_ctm.c = 0.0f;
    anim->root_ctm.d = scale;
    anim->root_ctm.e = 0.0f;
    anim->root_ctm.f = 0.0f;

    return YETTY_OK(yetty_ylottie_animation_ptr, anim);
}

struct yetty_ylottie_info yetty_ylottie_animation_info(const struct yetty_ylottie_animation *anim)
{
    if (!anim) {
        struct yetty_ylottie_info empty = {0};
        return empty;
    }
    return anim->info;
}

struct yetty_ylottie_render_result yetty_ylottie_animation_render_frame(
    const struct yetty_ylottie_animation *anim, float frame, uint32_t bg_abgr)
{
    if (!anim) {
        return YETTY_ERR(yetty_ylottie_render, "ylottie: NULL animation");
    }
    struct yetty_ydraw_drawable_list_config bcfg = {.scene_min_x = 0.0f,
                                                    .scene_min_y = 0.0f,
                                                    .scene_max_x = anim->scene_w,
                                                    .scene_max_y = anim->scene_h};
    struct yetty_ydraw_drawable_list_result br =
        yetty_ydraw_drawable_list_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(br)) {
        return YETTY_ERR(yetty_ylottie_render, "ylottie: buffer create failed", br);
    }
    struct yetty_ydraw_drawable_list *buf = br.value;

    if ((bg_abgr >> 24) != 0) {
        struct yetty_ysdf_box bg = {.center_x = anim->scene_w * 0.5f,
                                    .center_y = anim->scene_h * 0.5f,
                                    .half_width = anim->scene_w * 0.5f,
                                    .half_height = anim->scene_h * 0.5f,
                                    .corner_radius = 0.0f};
        struct yetty_ycore_void_result r =
            yetty_ydraw_drawable_list_add_cmd_add_box(buf, 0, 0, bg_abgr, 0, 0.0f, &bg);
        if (YETTY_IS_ERR(r)) {
            yetty_ydraw_drawable_list_destroy(buf);
            return YETTY_ERR(yetty_ylottie_render, "ylottie: background emit failed", r);
        }
    }

    struct yetty_ylottie_paint_ctx ctx = {
        .buf = buf,
        .layers = anim->layers,
        .frame = frame,
        .default_font_size = anim->default_font_size,
        .root_ctm = anim->root_ctm,
        .user_to_pixel_scale = anim->root_ctm.a,
    };
    struct yetty_ycore_void_result er = yetty_ylottie_paint(&ctx);
    if (YETTY_IS_ERR(er)) {
        yetty_ydraw_drawable_list_destroy(buf);
        return YETTY_ERR(yetty_ylottie_render, "ylottie: emission failed", er);
    }

    struct yetty_ylottie_render_output out = {
        .buffer = buf, .scene_width = anim->scene_w, .scene_height = anim->scene_h};
    return YETTY_OK(yetty_ylottie_render, out);
}

void yetty_ylottie_animation_destroy(struct yetty_ylottie_animation *anim)
{
    if (!anim) {
        return;
    }
    yetty_ylottie_doc_destroy(anim->doc);
    free(anim);
}

/*=============================================================================
 * Public entry point — one-shot single-frame wrapper over the animation API
 *===========================================================================*/

struct yetty_ylottie_render_result yetty_ylottie_render(
    const char *content, size_t content_len, const char *args, size_t args_len,
    const struct yetty_ylottie_render_config *config)
{
    struct ylottie_params params = {0};
    params_parse(args, args_len, &params);

    struct yetty_ylottie_animation_ptr_result ar =
        yetty_ylottie_animation_create(content, content_len, config);
    if (YETTY_IS_ERR(ar)) {
        return YETTY_ERR(yetty_ylottie_render, "ylottie: animation create failed", ar);
    }
    struct yetty_ylottie_animation *anim = ar.value;
    struct yetty_ylottie_info info = anim->info;

    /* Frame to render: --time wins over --frame; default = in-point. */
    float frame = info.in_point;
    if (params.has_frame) {
        frame = params.frame;
    }
    if (params.has_time) {
        frame = params.time_seconds * (info.frame_rate > 0.0f ? info.frame_rate : 30.0f);
    }
    uint32_t bg = (params.has_bg && (params.bg_abgr >> 24) != 0) ? params.bg_abgr : 0;

    struct yetty_ylottie_render_result rr = yetty_ylottie_animation_render_frame(anim, frame, bg);
    yetty_ylottie_animation_destroy(anim);
    return rr;
}
