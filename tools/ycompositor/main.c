/*
 * tools/ycompositor/main.c — standalone ycompositor test harness.
 *
 * Opens a window via yinit_run + yframework_create, builds a ycompositor
 * with a single full-window ygrid figure, and pushes a parameter-sweep
 * of SDF primitives directly into the grid via yetty_ygrid_add_record.
 * No terminal, no yui, no ygui — the compositor and ygrid render path
 * are exercised in isolation.
 *
 * Event loop:
 *   - drain platform_input_pipe
 *   - on RESIZE: reconfigure surface, resize render target, rebuild
 *     the figure to the new bounds.
 *   - each frame: target->clear → ycompositor_render → target->present.
 *
 * Records are hand-built as u32 arrays per the SDF wire format:
 *
 *   [0] type (e.g. YETTY_YSDF_ROUNDED_BOX)
 *   [1] z_order
 *   [2] fill_color  (0xAARRGGBB)
 *   [3] stroke_color
 *   [4] stroke_width (f32 bits)
 *   [5..] geometry (f32; struct yetty_ysdf_<kind>)
 */

#include <yetty/yinit/yinit.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>
#include <yetty/yplatform/extract-assets.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/container.h>
#include <yetty/yfigure/registry.h>
#include <yetty/yfigure/rpc.h>
#include <yetty/yfigure/wire.h>
#include <yetty/yfont/font.h>
#include <yetty/yfont/msdf-font.h>
#include <yetty/ygrid/ygrid.h>
#include <yetty/yfigure/figure.h>
#include <yetty/ysdf/types.gen.h>
#include <yetty/ytrace/ytrace.h>
#include <webgpu/webgpu.h>
#include <yetty/yplatform/io.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct ycomp_app {
    int quit;
    struct yetty_context       ctx;
    struct yetty_yframework     *yrt;
    struct yetty_ydraw_target *target;  /* our own texture target, bypasses
                                         * yframework's x11-tile choice */
    struct yetty_yfigure_container *root;
    struct yetty_ygrid_grid   *grid;
    /* Default font loaded once at worker startup and attached to every
     * rebuilt grid at slot 0. Owned here, destroyed in teardown. */
    struct yetty_yfont_font   *font;
    void                       *surface; /* WGPUSurface borrowed from yinit_rt */
    uint32_t                    surface_w;
    uint32_t                    surface_h;
};

/* Mint brand color from rules/08-branding.md: BRAND_ACCENT #6BA892, etc.
 * Format on wire is 0xAARRGGBB (the same packing the dashboard uses). */
#define COL_BG_LIFTED   0xFF141A1Fu
#define COL_BG_ROW      0xFF1E262Cu
#define COL_BORDER      0xFF364A47u
#define COL_TEXT_PRI    0xFFE0E5E4u
#define COL_ACCENT_DEEP 0xFF5A8979u
#define COL_ACCENT      0xFF6BA892u
#define COL_ACCENT_HI   0xFF74C5A5u

static uint32_t f32_bits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

/* Build one SDF record into `out` and feed it to the grid. The caller
 * fills the geometry words after the 5-word header (type, z_order, fill,
 * stroke, stroke_width). Returns the void result so failures propagate. */
static struct yetty_ycore_void_result
push_sdf(struct yetty_ygrid_grid *g,
         uint32_t type, uint32_t z_order, uint32_t fill,
         uint32_t stroke, float stroke_w,
         const float *geom, size_t geom_words)
{
    /* Max SDF prim today is LINEAR_GRADIENT_BOX at 16 words. */
    uint32_t buf[32];
    if (5u + geom_words > sizeof(buf) / sizeof(buf[0]))
        return YETTY_ERR(yetty_ycore_void, "push_sdf: prim too large");
    buf[0] = type;
    buf[1] = z_order;
    buf[2] = fill;
    buf[3] = stroke;
    buf[4] = f32_bits(stroke_w);
    for (size_t i = 0; i < geom_words; i++)
        memcpy(&buf[5 + i], &geom[i], sizeof(float));
    return yetty_ygrid_add_record_local(g, (const uint8_t *)buf,
                                        (5u + geom_words) * sizeof(uint32_t));
}

/* Convenience wrappers — one per SDF kind we exercise. */
static struct yetty_ycore_void_result
emit_rounded_box(struct yetty_ygrid_grid *g, uint32_t fill, uint32_t stroke, float sw,
                 float cx, float cy, float hw, float hh, float r)
{
    float geom[8] = {cx, cy, hw, hh, r, r, r, r};
    return push_sdf(g, YETTY_YSDF_ROUNDED_BOX, 0, fill, stroke, sw, geom, 8);
}

static struct yetty_ycore_void_result
emit_box(struct yetty_ygrid_grid *g, uint32_t fill, uint32_t stroke, float sw,
         float cx, float cy, float hw, float hh)
{
    float geom[5] = {cx, cy, hw, hh, 0.0f};
    return push_sdf(g, YETTY_YSDF_BOX, 0, fill, stroke, sw, geom, 5);
}

static struct yetty_ycore_void_result
emit_circle(struct yetty_ygrid_grid *g, uint32_t fill, uint32_t stroke, float sw,
            float cx, float cy, float r)
{
    float geom[3] = {cx, cy, r};
    return push_sdf(g, YETTY_YSDF_CIRCLE, 0, fill, stroke, sw, geom, 3);
}

static struct yetty_ycore_void_result
emit_ellipse(struct yetty_ygrid_grid *g, uint32_t fill, uint32_t stroke, float sw,
             float cx, float cy, float rx, float ry)
{
    float geom[4] = {cx, cy, rx, ry};
    return push_sdf(g, YETTY_YSDF_ELLIPSE, 0, fill, stroke, sw, geom, 4);
}

static struct yetty_ycore_void_result
emit_triangle(struct yetty_ygrid_grid *g, uint32_t fill, uint32_t stroke, float sw,
              float ax, float ay, float bx, float by, float cx, float cy)
{
    float geom[6] = {ax, ay, bx, by, cx, cy};
    return push_sdf(g, YETTY_YSDF_TRIANGLE, 0, fill, stroke, sw, geom, 6);
}

static struct yetty_ycore_void_result
emit_segment(struct yetty_ygrid_grid *g, uint32_t stroke, float sw,
             float x0, float y0, float x1, float y1)
{
    float geom[4] = {x0, y0, x1, y1};
    return push_sdf(g, YETTY_YSDF_SEGMENT, 0, 0, stroke, sw, geom, 4);
}

static struct yetty_ycore_void_result
emit_star(struct yetty_ygrid_grid *g, uint32_t fill, uint32_t stroke, float sw,
          float cx, float cy, float r, float points, float inner_ratio)
{
    float geom[5] = {cx, cy, r, points, inner_ratio};
    return push_sdf(g, YETTY_YSDF_STAR, 0, fill, stroke, sw, geom, 5);
}

static struct yetty_ycore_void_result
emit_hexagon(struct yetty_ygrid_grid *g, uint32_t fill, uint32_t stroke, float sw,
             float cx, float cy, float r)
{
    float geom[3] = {cx, cy, r};
    return push_sdf(g, YETTY_YSDF_HEXAGON, 0, fill, stroke, sw, geom, 3);
}

static struct yetty_ycore_void_result
emit_heart(struct yetty_ygrid_grid *g, uint32_t fill, uint32_t stroke, float sw,
           float cx, float cy, float scale)
{
    float geom[3] = {cx, cy, scale};
    return push_sdf(g, YETTY_YSDF_HEART, 0, fill, stroke, sw, geom, 3);
}

static struct yetty_ycore_void_result
emit_capsule(struct yetty_ygrid_grid *g, uint32_t fill, uint32_t stroke, float sw,
             float x0, float y0, float x1, float y1, float r)
{
    float geom[5] = {x0, y0, x1, y1, r};
    return push_sdf(g, YETTY_YSDF_CAPSULE, 0, fill, stroke, sw, geom, 5);
}

/* Emit a TEXT_SPAN record. ygrid expands it into one GLYPH record per
 * codepoint at index time (same flow scene-canvas uses), so layout,
 * bearings, and advances come from the actual font metrics — the
 * caller just hands over the UTF-8 string.
 *
 * Wire format documented in include/yetty/ydraw-core/text-drawable-list.h:
 *
 *   u32 type            (= YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST = 0x40000002)
 *   u32 payload_size    (bytes of payload, padded to 4)
 *   f32 x, y, font_size, rotation
 *   u32 color, layer
 *   i32 font_id         (matches the slot set via yetty_ygrid_set_font,
 *                         -1 means "default" → slot 0)
 *   u32 text_len
 *   f32 char_spacing, word_spacing
 *   u8  text[text_len]
 *   u8  pad[0..3]
 */
static struct yetty_ycore_void_result
emit_text_span(struct yetty_ygrid_grid *grid, int32_t font_slot, const char *utf8,
               float x, float y, float font_size, uint32_t color)
{
    enum { HEADER_BYTES = 8, FIXED_BYTES = 40 };
    uint32_t text_len = (uint32_t)strlen(utf8);
    uint32_t payload_unaligned = FIXED_BYTES + text_len;
    uint32_t payload_size = (payload_unaligned + 3u) & ~3u;
    size_t record_size = HEADER_BYTES + payload_size;

    uint8_t *record = (uint8_t *)calloc(1, record_size);
    if (!record)
        return YETTY_ERR(yetty_ycore_void, "emit_text_span: record alloc oom");

    uint32_t type = 0x40000002u;            /* YETTY_YDRAW_TYPE_TEXT_DRAWABLE_LIST */
    uint32_t layer = 0u;
    float rotation = 0.0f;
    float char_spacing = 0.0f;
    float word_spacing = 0.0f;

    memcpy(record + 0,  &type,         4);
    memcpy(record + 4,  &payload_size, 4);
    uint8_t *payload = record + HEADER_BYTES;
    memcpy(payload + 0,  &x,            4);
    memcpy(payload + 4,  &y,            4);
    memcpy(payload + 8,  &font_size,    4);
    memcpy(payload + 12, &rotation,     4);
    memcpy(payload + 16, &color,        4);
    memcpy(payload + 20, &layer,        4);
    memcpy(payload + 24, &font_slot,    4);
    memcpy(payload + 28, &text_len,     4);
    memcpy(payload + 32, &char_spacing, 4);
    memcpy(payload + 36, &word_spacing, 4);
    if (text_len > 0)
        memcpy(payload + 40, utf8, text_len);

    struct yetty_ycore_void_result add_result =
        yetty_ygrid_add_record_local(grid, record, record_size);
    free(record);
    return add_result;
}

/* Lay out a parameter-sweep grid: one column per SDF kind, three rows
 * showing fill / stroke-only / stroke+fill variants, plus a TEXT_SPAN
 * header at the top to exercise the font dispatcher path. Coordinates
 * are LOCAL to the ygrid figure (the figure's rect handles translation
 * into target space). `has_text` gates the TEXT_SPAN emission — set
 * when a font has been attached at slot 0 so the expansion path can
 * resolve glyphs. */
static struct yetty_ycore_void_result
populate_grid(struct yetty_ygrid_grid *grid, int has_text, float w, float h)
{
    /* Background — one big rounded-box covers the figure. */
    {
        struct yetty_ycore_void_result r = emit_rounded_box(
            grid, COL_BG_LIFTED, COL_BORDER, 2.0f,
            w * 0.5f, h * 0.5f, w * 0.5f - 8.0f, h * 0.5f - 8.0f, 16.0f);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "bg rounded box");
    }

    /* Grid: 4 columns × 3 rows of shapes, plus a row of strokes below. */
    const int cols = 4;
    const int rows = 3;
    float pad_x = 40.0f;
    float pad_y = 60.0f;
    float cell_w = (w - 2.0f * pad_x) / (float)cols;
    float cell_h = (h - 2.0f * pad_y - 80.0f) / (float)rows;
    float half = (cell_w < cell_h ? cell_w : cell_h) * 0.35f;

    struct yetty_ycore_void_result r;
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            float cx = pad_x + (col + 0.5f) * cell_w;
            float cy = pad_y + (row + 0.5f) * cell_h;
            uint32_t fill = 0, stroke = 0;
            float sw = 0.0f;
            switch (row) {
            case 0: fill = COL_ACCENT;      stroke = 0;            sw = 0.0f; break;
            case 1: fill = 0;               stroke = COL_ACCENT_HI; sw = 3.0f; break;
            case 2: fill = COL_ACCENT_DEEP; stroke = COL_TEXT_PRI; sw = 2.0f; break;
            }
            int kind = col + (row * cols);
            switch (kind % 10) {
            case 0:
                r = emit_rounded_box(grid, fill, stroke, sw, cx, cy, half, half * 0.7f, 8.0f);
                break;
            case 1:
                r = emit_box(grid, fill, stroke, sw, cx, cy, half, half * 0.6f);
                break;
            case 2:
                r = emit_circle(grid, fill, stroke, sw, cx, cy, half * 0.9f);
                break;
            case 3:
                r = emit_ellipse(grid, fill, stroke, sw, cx, cy, half, half * 0.55f);
                break;
            case 4:
                r = emit_triangle(grid, fill, stroke, sw,
                                  cx,         cy - half,
                                  cx - half,  cy + half * 0.7f,
                                  cx + half,  cy + half * 0.7f);
                break;
            case 5:
                r = emit_hexagon(grid, fill, stroke, sw, cx, cy, half);
                break;
            case 6:
                r = emit_star(grid, fill, stroke, sw, cx, cy, half, 5.0f, 0.5f);
                break;
            case 7:
                r = emit_heart(grid, fill, stroke, sw, cx, cy + half * 0.2f, half * 0.9f);
                break;
            case 8:
                r = emit_capsule(grid, fill, stroke, sw,
                                 cx - half * 0.7f, cy,
                                 cx + half * 0.7f, cy,
                                 half * 0.4f);
                break;
            default:
                r = emit_rounded_box(grid, fill, stroke, sw, cx, cy, half, half * 0.5f, 4.0f);
                break;
            }
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "populate_grid: emit prim");
        }
    }

    /* Line band along the bottom — pure stroke segments at varying widths. */
    float line_y = h - 40.0f;
    for (int i = 0; i < 8; i++) {
        float x0 = pad_x + (float)i * (w - 2.0f * pad_x) / 8.0f;
        float x1 = x0 + (w - 2.0f * pad_x) / 9.0f;
        r = emit_segment(grid, COL_ACCENT_HI, 1.0f + (float)i * 0.8f,
                         x0, line_y, x1, line_y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "populate_grid: line");
    }

    /* Header TEXT_SPAN — exercises the TEXT_SPAN → glyph expansion +
     * dispatcher path (slot 0, the default font attached in
     * rebuild_figure). Skipped when no font is wired so the SDF sweep
     * keeps rendering. font_id = -1 addresses the default slot. */
    if (has_text) {
        struct yetty_ycore_void_result text_result = emit_text_span(
            grid, /*font_slot=*/-1, "ycompositor: ygrid + TEXT_SPAN",
            pad_x, 36.0f, 28.0f, COL_TEXT_PRI);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, text_result, "populate_grid: header text");
    }
    return YETTY_OK_VOID();
}

/* (Re)build the single ygrid figure to cover the whole drawable area
 * minus a small margin. Called at startup and on RESIZE. */
static struct yetty_ycore_void_result
rebuild_figure(struct ycomp_app *app)
{
    const float margin = 20.0f;
    struct yetty_ycore_rectangle rect = {
        .min = {.x = margin,                                .y = margin},
        .max = {.x = (float)app->surface_w - margin,        .y = (float)app->surface_h - margin},
    };
    float w = rect.max.x - rect.min.x;
    float h = rect.max.y - rect.min.y;
    if (w <= 0.0f || h <= 0.0f)
        return YETTY_OK_VOID();      /* window too small — try again next resize */

    /* Drop the old figure if present. remove_child_by_id destroys the
     * child as part of the same call (uthash entry + figure cascade). */
    if (app->grid) {
        struct yetty_ycore_void_result rr =
            yetty_yfigure_container_remove_child_by_id(app->root, 1u);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr, "rebuild_figure: remove old");
        app->grid = NULL;
    }

    /* Grid bucket count — coarse is fine; the GPU pipeline handles
     * dispatch. 32×16 keeps the per-cell list short for our handful of
     * prims. */
    struct yetty_ygrid_grid_ptr_result gr =
        yetty_ygrid_create(rect, 32u, 16u, &app->ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, gr, "rebuild_figure: ygrid_create");
    app->grid = gr.value;

    /* Attach the default font at slot 0 before populating so GLYPH
     * records emitted during populate_grid can address it directly. */
    if (app->font) {
        struct yetty_ycore_void_result font_result =
            yetty_ygrid_set_font(app->grid, 0u, app->font);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, font_result, "rebuild_figure: set_font");
    }

    struct yetty_ycore_void_result pr =
        populate_grid(app->grid, /*has_text=*/app->font != NULL, w, h);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, pr, "rebuild_figure: populate_grid");

    struct yetty_ycore_void_result ar = yetty_yfigure_container_add_child(
        app->root, yetty_ygrid_as_figure(app->grid), /*id=*/1u);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, ar, "rebuild_figure: add_child");

    yinfo("ycompositor: figure rebuilt for %ux%u (rect %.1fx%.1f)",
          app->surface_w, app->surface_h, w, h);
    return YETTY_OK_VOID();
}

static void handle_event(struct ycomp_app *app, const struct yetty_yui_event *ev)
{
    switch (ev->type) {
    case YETTY_YCORE_SHUTDOWN:
    case YETTY_YCORE_WINDOW_CLOSE:
        app->quit = 1;
        return;
    case YETTY_YCORE_RESIZE: {
        uint32_t w = (uint32_t)ev->resize.width;
        uint32_t h = (uint32_t)ev->resize.height;
        if (w == 0 || h == 0) return;
        app->surface_w = w;
        app->surface_h = h;
        struct yetty_ycore_void_result rr =
            yetty_yframework_reconfigure_surface(app->yrt, w, h);
        if (YETTY_IS_ERR(rr)) {
            yerror("ycompositor: reconfigure_surface failed: %s", rr.error.msg);
            yetty_ycore_error_destroy(rr.error);
        }
        struct yetty_yrender_viewport vp = {.x = 0, .y = 0, .w = (float)w, .h = (float)h};
        struct yetty_ycore_void_result tr =
            app->target->ops->resize(app->target, vp);
        if (YETTY_IS_ERR(tr)) {
            yerror("ycompositor: render_target resize failed: %s", tr.error.msg);
            yetty_ycore_error_destroy(tr.error);
        }
        struct yetty_ycore_void_result fr = rebuild_figure(app);
        if (YETTY_IS_ERR(fr)) {
            yerror("ycompositor: rebuild_figure failed: %s", fr.error.msg);
            yetty_ycore_error_destroy(fr.error);
        }
        return;
    }
    case YETTY_YCORE_KEY_DOWN:
        /* GLFW key codes: 256=ESC, 81=Q. */
        if (ev->key.key == 256 || ev->key.key == 81) {
            app->quit = 1;
        }
        return;
    default:
        return;
    }
}

static struct yetty_ycore_void_result
ycomp_worker(struct yetty_yinit_runtime *rt, void *user)
{
    struct ycomp_app *app = user;

    struct yetty_yframework_ptr_result yr = yetty_yframework_create(rt);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, yr, "yframework_create failed");
    app->yrt = yr.value;

    /* Build the context the compositor + ygrid expect. pty_factory is
     * absent (no terminal); event_loop comes from yframework. */
    app->ctx.runtime     = app->yrt;
    app->ctx.pty_factory = NULL;
    app->ctx.event_loop  = app->yrt->event_loop;

    app->surface   = rt->surface;
    app->surface_w = rt->surface_width;
    app->surface_h = rt->surface_height;

    /* yframework auto-picks an X11-tile render target on Linux/X11; its
     * present() pipeline is async (libuv + wgpu buffer-map callbacks)
     * and needs the event loop pumped. The simple poll loop here can't
     * drive that, so swap it out for a plain texture target that
     * blits to the GLFW surface on present. yframework_create always
     * yields a non-NULL render_target — a NULL here is a contract
     * violation; let it crash. */
    app->yrt->render_target->ops->destroy(app->yrt->render_target);
    app->yrt->render_target = NULL;
    struct yetty_yrender_viewport vp = {
        .x = 0, .y = 0,
        .w = (float)app->surface_w, .h = (float)app->surface_h,
    };
    struct yetty_yrender_target_ptr_result tr = yetty_yrender_target_texture_create(
        app->yrt->gpu.device, app->yrt->gpu.queue, app->yrt->gpu.surface_format,
        app->yrt->gpu.allocator, (WGPUSurface)app->surface, vp);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "texture target create failed");
    app->target = tr.value;

    /* Root container — owns the demo's ygrid figure. No registry
     * needed: this tool adds the ygrid directly via add_child rather
     * than going through wire admin CREATE_CHILD. */
    struct yetty_ycore_rectangle root_rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = (float)app->surface_w, .y = (float)app->surface_h},
    };
    struct yetty_yclass_ctx yclass_ctx = {0};
    struct yetty_yclass_object_ptr_result obj_res =
        yetty_yfigure_container_create(&yclass_ctx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, obj_res, "root container create failed");
    app->root = yetty_yfigure_container_from(obj_res.value);
    yetty_yfigure_container_set_context(app->root, &app->ctx);
    /* registry stays NULL — this tool runs without a figure registry. */
    yetty_yfigure_container_set_rect(app->root, root_rect);

    /* Load the default MSDF font once and pre-cache basic-latin glyphs
     * so populate_grid's text emission can resolve every codepoint
     * without a per-glyph atlas miss / re-upload. Same path layout
     * scrolling-canvas uses: `<paths/fonts>/../msdf-fonts/<family>-Regular.cdb`
     * for the CDB, `<paths/shaders>/msdf-font.wgsl` for the shader. */
    struct yetty_yconfig_config *config = app->yrt->config;
    const char *fonts_dir   = config->ops->get_string(config, "paths/fonts", "");
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    const char *font_family = "DejaVuSansMNerdFontMono";
    char cdb_path[768];
    char shader_path[768];
    snprintf(cdb_path, sizeof(cdb_path),
             "%s/../msdf-fonts/%s-Regular.cdb", fonts_dir, font_family);
    snprintf(shader_path, sizeof(shader_path),
             "%s/msdf-font.wgsl", shaders_dir);
    yinfo("ycompositor: loading font cdb='%s' shader='%s'", cdb_path, shader_path);
    struct yetty_font_font_result font_result =
        yetty_yfont_msdf_font_create(cdb_path, shader_path, "ycompositor_default");
    YETTY_RETURN_IF_ERR(yetty_ycore_void, font_result, "msdf_font_create failed");
    app->font = font_result.value;
    struct yetty_ycore_void_result load_result =
        app->font->ops->load_basic_latin(app->font);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, load_result, "font load_basic_latin failed");

    struct yetty_ycore_void_result fr = rebuild_figure(app);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "rebuild_figure (initial) failed");

    /* Event-driven render loop — same shape as yaudio. */
    struct yetty_ycore_int_result fdr =
        rt->platform_input_pipe->ops->read_fd(rt->platform_input_pipe);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fdr, "pipe read_fd failed");
    int pipe_fd = fdr.value;

    int needs_render = 1;
    while (!app->quit) {
        int pr = yetty_yplatform_io_wait_readable(pipe_fd, needs_render ? 0 : -1);
        int had_events = 0;
        if (pr > 0) {
            for (;;) {
                struct yetty_yui_event ev = {0};
                struct yetty_ycore_size_result rr =
                    rt->platform_input_pipe->ops->read(rt->platform_input_pipe,
                                                       &ev, sizeof(ev));
                if (YETTY_IS_ERR(rr) || rr.value != sizeof(ev)) break;
                handle_event(app, &ev);
                had_events = 1;
            }
        }
        if (rt->instance) {
            wgpuInstanceProcessEvents((WGPUInstance)rt->instance);
        }
        struct yetty_yfigure_figure *rf =
            yetty_yfigure_container_as_figure(app->root);
        if (!(needs_render || had_events || yetty_yfigure_figure_dirty_get((struct yetty_yclass_object *)(rf) - 1).value)) {
            continue;
        }

        struct yetty_ydraw_target *target = app->target;
        struct yetty_ycore_void_result cl = target->ops->clear(target);
        if (YETTY_IS_ERR(cl)) {
            yerror("ycompositor: clear failed: %s", cl.error.msg);
            yetty_ycore_error_destroy(cl.error);
        }
        struct yetty_ycore_void_result rr =
            yetty_yfigure_render(NULL, (struct yetty_yclass_object *)rf - 1, target);
        if (YETTY_IS_ERR(rr)) {
            yerror("ycompositor: root render failed: %s", rr.error.msg);
            yetty_ycore_error_destroy(rr.error);
        } else {
            yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(rf) - 1, 0);
        }
        struct yetty_ycore_void_result pp = target->ops->present(target);
        if (YETTY_IS_ERR(pp)) {
            yerror("ycompositor: present failed: %s", pp.error.msg);
            yetty_ycore_error_destroy(pp.error);
        }
        needs_render = 0;
    }

    /* Strict teardown: root container + figures before yframework so any
     * pending GPU work bound to the runtime's device flushes first. */
    {
        struct yetty_yfigure_figure *rf =
            yetty_yfigure_container_as_figure(app->root);
        struct yetty_ycore_void_result dr =
            yetty_yfigure_destroy(NULL, (struct yetty_yclass_object *)rf - 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, dr, "root destroy");
    }
    app->root = NULL;
    app->grid = NULL;       /* destroyed by root cascade */

    app->target->ops->destroy(app->target);
    app->target = NULL;

    /* Font outlived every ygrid that borrowed it via set_font, so it's
     * safe to destroy after the compositor cascade above. */
    if (app->font) {
        app->font->ops->destroy(app->font);
        app->font = NULL;
    }

    yetty_yframework_destroy(app->yrt);
    app->yrt = NULL;
    return YETTY_OK_VOID();
}

int main(int argc, char **argv)
{
    struct ycomp_app app = {0};
    struct yetty_yinit_app_config cfg = {
        .extract_assets_fn = yetty_platform_extract_assets,
    };
    return yetty_yinit_run(argc, argv, &cfg, ycomp_worker, &app);
}
