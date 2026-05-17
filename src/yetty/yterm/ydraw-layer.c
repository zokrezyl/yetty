#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/util.h>
#include <yetty/yfont/font.h>
#include <yetty/yface/yface.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/ydraw-core/figure-types.h>
#include <yetty/ydraw-factory/figure-factory.h>
#include <yetty/ydraw/canvas.h>
#include <yetty/ydraw/scene-canvas.h>
#include <yetty/ydraw/scrolling-canvas.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterm/osc-args.h>
#include <yetty/yterm/osc-codes.h> /* YETTY_OSC_YDRAW_* */
#include <yetty/yterm/ydraw-layer.h>
#include <yetty/yconfig/config.h>
#include <yetty/yetty/yetty.h>
#include <yetty/ytrace/ytrace.h>

/* Uniform positions */
#define U_GRID_SIZE 0
#define U_CELL_SIZE 1
#define U_ROLLING_ROW_0 2
#define U_PRIM_COUNT 3
#define U_VZ_SCALE 4
#define U_VZ_OFF 5
#define U_CZ_SCALE 6
#define U_CZ_OFF 7
#define U_COUNT 8

/* Setters */
static inline void set_grid_size(struct yetty_ydraw_gpu_resource_set *rs, float cols, float rows)
{
    rs->uniforms[U_GRID_SIZE].vec2[0] = cols;
    rs->uniforms[U_GRID_SIZE].vec2[1] = rows;
}
static inline void set_cell_size(struct yetty_ydraw_gpu_resource_set *rs, float w, float h)
{
    rs->uniforms[U_CELL_SIZE].vec2[0] = w;
    rs->uniforms[U_CELL_SIZE].vec2[1] = h;
}
static inline void set_rolling_row_0(struct yetty_ydraw_gpu_resource_set *rs, uint32_t row_origin)
{
    rs->uniforms[U_ROLLING_ROW_0].u32 = row_origin;
}
static inline void set_drawable_count(struct yetty_ydraw_gpu_resource_set *rs, uint32_t count)
{
    rs->uniforms[U_PRIM_COUNT].u32 = count;
}
static inline void set_visual_zoom(struct yetty_ydraw_gpu_resource_set *rs, float scale,
                                   float off_x, float off_y)
{
    rs->uniforms[U_VZ_SCALE].f32 = scale;
    rs->uniforms[U_VZ_OFF].vec2[0] = off_x;
    rs->uniforms[U_VZ_OFF].vec2[1] = off_y;
}
static inline void set_cell_zoom(struct yetty_ydraw_gpu_resource_set *rs, float scale, float off_x,
                                 float off_y)
{
    rs->uniforms[U_CZ_SCALE].f32 = scale;
    rs->uniforms[U_CZ_OFF].vec2[0] = off_x;
    rs->uniforms[U_CZ_OFF].vec2[1] = off_y;
}

/* Init uniforms */
static void init_uniforms(struct yetty_ydraw_gpu_resource_set *rs)
{
    rs->uniform_count = U_COUNT;

    rs->uniforms[U_GRID_SIZE] =
        (struct yetty_yrender_uniform){"ydraw_grid_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_CELL_SIZE] =
        (struct yetty_yrender_uniform){"ydraw_cell_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_ROLLING_ROW_0] =
        (struct yetty_yrender_uniform){"ydraw_rolling_row_0", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_PRIM_COUNT] =
        (struct yetty_yrender_uniform){"ydraw_drawable_count", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_VZ_SCALE] =
        (struct yetty_yrender_uniform){"ydraw_visual_zoom_scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_VZ_OFF] =
        (struct yetty_yrender_uniform){"ydraw_visual_zoom_off", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_CZ_SCALE] =
        (struct yetty_yrender_uniform){"ydraw_cell_zoom_scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_CZ_OFF] =
        (struct yetty_yrender_uniform){"ydraw_cell_zoom_off", YETTY_YRENDER_UNIFORM_VEC2};

    set_rolling_row_0(rs, 0);
    set_drawable_count(rs, 0);
    set_visual_zoom(rs, 1.0f, 0.0f, 0.0f);
    set_cell_zoom(rs, 1.0f, 0.0f, 0.0f);
}

/* YDraw layer - embeds base as first member */
struct yetty_yterm_ydraw_layer {
    struct yetty_yrender_terminal_layer base;
    /* Initial cell size captured at creation — used to derive the cumulative
   * cell-zoom factor and push it to complex-prim factories (yplot, …) so
   * their shaders can apply the "intrusive" zoom the same way they apply
   * the non-intrusive visual zoom. */
    struct yetty_ycore_pixel_size initial_cell_size;

    /* Child resource set that holds the generated SDF library (ysdf.gen.wgsl:
   * sdf_* functions + evaluate_sdf_2d dispatcher). Merged into the final
   * shader by the binder via rs.children[], so the layer never has to
   * handwrite SDF cases — regenerate the .wgsl via gen-sdf-code.py instead. */
    struct yetty_ycore_buffer sdf_lib_code;
    struct yetty_ydraw_gpu_resource_set sdf_lib_rs;
    struct yetty_ydraw_canvas *canvas;
    struct yetty_ydraw_gpu_resource_set rs;
    struct yetty_ycore_buffer shader_code;

    /* Combined layer shader: generated `font_glyph_*` dispatcher block
     * (one switch case per canvas font) prepended to the static
     * shader_code above. Regenerated whenever the canvas's font-cache
     * generation changes — i.e. on any slot alloc OR release. Keying
     * on font_count alone misses drops (count is a high watermark) and
     * leaves stale `case Nu: msdf_<hex>_*` lines referencing structs the
     * binder no longer merges into the module. The pipeline keys
     * recompiles on the layer's shader hash, so changing this triggers
     * a recompile. */
    char *combined_shader;
    size_t combined_shader_size;
    uint32_t last_font_count;      /* kept for diagnostics / log lines */
    uint32_t last_font_generation; /* cache generation baked into combined */

    /* Staging buffers - point to canvas data */
    uint8_t *grid_staging;
    size_t grid_staging_size;
    uint8_t *drawable_staging;
    size_t drawable_staging_size;

    /* yface — kept for OUTGOING emit only (focus events, ymgui responses,
     * etc). Incoming decode now lives in the OSC SM. */
    struct yetty_yface *yface;

    /* Variant kind chosen at create — remembered so the lazy alt-screen
   * canvas (built on first ?1049 toggle) matches the live canvas's
   * variant. */
    enum yetty_yterm_ydraw_layer_kind kind;

    /* Cached at create — needed to lazily build the alt-screen canvas
   * on first ?1049 toggle. */
    const struct yetty_context *create_context;

    /* Alt-screen state. The active canvas is `canvas` above; the saved
   * counterpart (primary while in alt, alt while in primary) lives
   * here. Toggle via ydraw_layer_set_alt_screen swaps the two. */
    int alt_active;
    struct yetty_ydraw_canvas *saved_canvas;

    /* Selection — row range only. The column part of (anchor, head) is
     * meaningless for rich content, so we collapse it to [min_row, max_row].
     * Today the layer doesn't emit text yet (no row→drawable picker is
     * wired up); the storage is here so the picker can land without
     * touching every call site again. */
    int sel_active;
    uint32_t sel_min_row;
    uint32_t sel_max_row;
};

/* Forward declarations */
static struct yetty_ycore_void_result ydraw_layer_destroy(
    struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result ydraw_layer_process_input(
    struct yetty_yrender_terminal_layer *self,
    struct yetty_ywire_wire_statemachine *osc_statemachine);
static struct yetty_ycore_void_result ydraw_layer_resize_grid(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_grid_size grid_size);
static struct yetty_yrender_gpu_resource_set_result ydraw_layer_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self);
static int ydraw_layer_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods);
static int ydraw_layer_on_char(struct yetty_yrender_terminal_layer *self, uint32_t codepoint,
                               int mods);
static int ydraw_layer_is_empty(const struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result ydraw_layer_scroll(struct yetty_yrender_terminal_layer *self,
                                                         int lines);
static struct yetty_ycore_void_result ydraw_layer_set_cursor(
    struct yetty_yrender_terminal_layer *self, int col, int row);
static struct yetty_ycore_void_result ydraw_layer_render(struct yetty_yrender_terminal_layer *self,
                                                         struct yetty_ydraw_target *target);
static uint32_t ydraw_layer_get_live_anchor(const struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result ydraw_layer_set_view_top(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t view_top_total_idx);
static struct yetty_ycore_void_result ydraw_layer_set_alt_screen(
    struct yetty_yrender_terminal_layer *self, int active);
static struct yetty_ycore_void_result ydraw_layer_set_selection(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t anchor_row, uint32_t anchor_col,
    uint32_t head_row, uint32_t head_col);
static struct yetty_ycore_void_result ydraw_layer_get_selection_text(
    const struct yetty_yrender_terminal_layer *self, struct yetty_ycore_buffer *out);

/* Canvas scroll callback - propagate to other layers */
static struct yetty_ycore_void_result on_canvas_scroll(void *user_data, uint16_t num_lines)
{
    struct yetty_yterm_ydraw_layer *layer = user_data;
    ydebug("on_canvas_scroll ENTER: num_lines=%u in_external=%d", num_lines,
           layer->base.in_external_scroll);

    /* If in_external_scroll is set, this scroll was triggered by another layer
   * and we should NOT propagate back to avoid double-scroll loop */
    if (layer->base.in_external_scroll) {
        ydebug("on_canvas_scroll: skipping (in_external_scroll)");
        return YETTY_OK_VOID();
    }

    if (!layer->base.scroll_fn) {
        yerror("on_canvas_scroll: scroll_fn is NULL");
        return YETTY_ERR(yetty_ycore_void, "scroll_fn is NULL");
    }
    struct yetty_ycore_void_result res =
        layer->base.scroll_fn(&layer->base, (int)num_lines, layer->base.scroll_userdata);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "on_canvas_scroll: scroll_fn failed");
    ydebug("on_canvas_scroll EXIT: num_lines=%u", num_lines);
    return YETTY_OK_VOID();
}

/* Canvas cursor callback - propagate to other layers */
static struct yetty_ycore_void_result on_canvas_cursor_set(void *user_data, uint16_t new_row)
{
    struct yetty_yterm_ydraw_layer *layer = user_data;
    ydebug("on_canvas_cursor_set ENTER: new_row=%u", new_row);
    if (!layer->base.cursor_fn) {
        return YETTY_ERR(yetty_ycore_void, "on_canvas_cursor_set: cursor_fn is NULL");
    }
    struct yetty_ycore_void_result r = layer->base.cursor_fn(
        &layer->base, (struct yetty_ycore_grid_cursor_pos){.cols = 0, .rows = new_row},
        layer->base.cursor_userdata);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "on_canvas_cursor_set: cursor_fn failed");
    ydebug("on_canvas_cursor_set EXIT: new_row=%u", new_row);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ydraw_layer_set_cell_size(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_pixel_size cell_size)
{
    struct yetty_yterm_ydraw_layer *layer = (struct yetty_yterm_ydraw_layer *)self;
    if (cell_size.width <= 0.0f || cell_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "invalid cell size");
    }
    if (!layer->canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }

    self->cell_size = cell_size;
    self->dirty = 1;

    /* Don't touch the canvas — keeping canvas cell_size/grid_size constant
     * preserves the existing drawable buckets (ydraw prims store absolute
     * pixel coords in the same frame the canvas was built in). The zoom is
     * achieved purely via a shader uniform transform, same mechanism as the
     * non-intrusive visual zoom, semantically separate (own uniform pair). */
    float base_h = layer->initial_cell_size.height;
    float cz = (base_h > 0.0f) ? (cell_size.height / base_h) : 1.0f;
    set_cell_zoom(&layer->rs, cz, 0.0f, 0.0f);

    /* Fan out to complex-prim factories so yplot and friends apply the
     * same transform in their own shaders. */
    struct yetty_ydraw_figure_factory *f = layer->canvas->ops->get_figure_factory(layer->canvas);
    yetty_ydraw_figure_factory_set_cell_zoom(f, cz, 0.0f, 0.0f);

    ydebug("ydraw_layer_set_cell_size: %.1fx%.1f cell_zoom=%.3f", cell_size.width, cell_size.height,
           cz);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ydraw_layer_set_visual_zoom(
    struct yetty_yrender_terminal_layer *self, float scale, float off_x, float off_y)
{
    struct yetty_yterm_ydraw_layer *layer = (struct yetty_yterm_ydraw_layer *)self;
    set_visual_zoom(&layer->rs, scale, off_x, off_y);
    self->dirty = 1;

    /* Complex drawables (yplot / yimage / …) render through their own
     * pipelines with their own fragment shaders — they don't go through the
     * ydraw-layer shader. Push the zoom into every concrete factory's shared
     * uniforms so each type's shader can apply the same transform. */
    if (layer->canvas) {
        struct yetty_ydraw_figure_factory *f =
            layer->canvas->ops->get_figure_factory(layer->canvas);
        yetty_ydraw_figure_factory_set_visual_zoom(f, scale, off_x, off_y);
    }
    return YETTY_OK_VOID();
}

/* Ops */
static const struct yetty_yterm_terminal_layer_ops ydraw_layer_ops = {
    .destroy = ydraw_layer_destroy,
    .process_input = ydraw_layer_process_input,
    .resize_grid = ydraw_layer_resize_grid,
    .set_cell_size = ydraw_layer_set_cell_size,
    .set_visual_zoom = ydraw_layer_set_visual_zoom,
    .get_gpu_resource_set = ydraw_layer_get_gpu_resource_set,
    .render = ydraw_layer_render,
    .is_empty = ydraw_layer_is_empty,
    .on_key = ydraw_layer_on_key,
    .on_char = ydraw_layer_on_char,
    .scroll = ydraw_layer_scroll,
    .set_cursor = ydraw_layer_set_cursor,
    .get_live_anchor = ydraw_layer_get_live_anchor,
    .set_view_top = ydraw_layer_set_view_top,
    .set_alt_screen = ydraw_layer_set_alt_screen,
    .set_selection = ydraw_layer_set_selection,
    .get_selection_text = ydraw_layer_get_selection_text,
};

/* Create */
struct yetty_yterm_terminal_layer_result yetty_yterm_ydraw_layer_create(
    enum yetty_yterm_ydraw_layer_kind kind, uint32_t cols, uint32_t rows, float cell_width,
    float cell_height, const struct yetty_context *context,
    yetty_yterm_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterm_scroll_fn scroll_fn, void *scroll_userdata, yetty_yterm_cursor_fn cursor_fn,
    void *cursor_userdata)
{
    struct yetty_yterm_ydraw_layer *layer;

    /* Load ydraw-layer shader from file */
    struct yetty_yconfig_config *config = context->app_context.config;
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char shader_path[512];
    char sdf_lib_path[512];
    snprintf(shader_path, sizeof(shader_path), "%s/ydraw-layer.wgsl", shaders_dir);
    /* Generated by src/yetty/ysdf/gen-sdf-code.py — single source of truth for
   * SDF dispatch. Attached below as a child rs; the binder merges its shader
   * source into ydraw-layer's compile. */
    snprintf(sdf_lib_path, sizeof(sdf_lib_path), "%s/ysdf.gen.wgsl", shaders_dir);

    struct yetty_ycore_buffer_result shader_res = yetty_ycore_read_file(shader_path);
    if (YETTY_IS_ERR(shader_res)) {
        return YETTY_ERR(yetty_yterm_terminal_layer,
                         "ydraw_layer_create: read_file(ydraw-layer.wgsl) failed", shader_res);
    }
    struct yetty_ycore_buffer_result sdf_lib_res = yetty_ycore_read_file(sdf_lib_path);
    if (YETTY_IS_ERR(sdf_lib_res)) {
        free(shader_res.value.data);
        return YETTY_ERR(yetty_yterm_terminal_layer,
                         "ydraw_layer_create: read_file(sdf_lib) failed", sdf_lib_res);
    }

    layer = calloc(1, sizeof(struct yetty_yterm_ydraw_layer));
    if (!layer) {
        free(shader_res.value.data);
        free(sdf_lib_res.value.data);
        return YETTY_ERR(yetty_yterm_terminal_layer, "failed to allocate ydraw layer");
    }
    layer->shader_code = shader_res.value;
    layer->sdf_lib_code = sdf_lib_res.value;
    strncpy(layer->sdf_lib_rs.namespace, "ysdf_lib", YETTY_YRENDER_NAME_MAX - 1);
    yetty_yrender_shader_code_set(&layer->sdf_lib_rs.shader, (const char *)layer->sdf_lib_code.data,
                                  layer->sdf_lib_code.size);

    layer->base.ops = &ydraw_layer_ops;
    layer->base.grid_size.cols = cols;
    layer->base.grid_size.rows = rows;
    layer->base.cell_size.width = cell_width;
    layer->base.cell_size.height = cell_height;
    layer->initial_cell_size.width = cell_width;
    layer->initial_cell_size.height = cell_height;
    layer->base.dirty = 0;
    layer->base.pty_write_fn = NULL; /* ydraw layer doesn't write to PTY */
    layer->base.pty_write_userdata = NULL;
    layer->base.request_render_fn = request_render_fn;
    layer->base.request_render_userdata = request_render_userdata;
    layer->base.scroll_fn = scroll_fn;
    layer->base.scroll_userdata = scroll_userdata;
    layer->base.cursor_fn = cursor_fn;
    layer->base.cursor_userdata = cursor_userdata;

    layer->create_context = context;
    layer->kind = kind;

    /* Create the canvas variant matching `kind`. */
    if (!context) {
        free(layer);
        return YETTY_ERR(yetty_yterm_terminal_layer, "context is NULL");
    }
    struct yetty_ydraw_canvas_ptr_result canvas_res =
        (kind == YETTY_YDRAW_LAYER_KIND_SCROLLING) ? yetty_ydraw_scrolling_canvas_create(context)
                                                   : yetty_ydraw_scene_canvas_create(context);
    if (YETTY_IS_ERR(canvas_res)) {
        free(layer);
        return YETTY_ERR(yetty_yterm_terminal_layer, "ydraw-layer: canvas create failed",
                         canvas_res);
    }
    layer->canvas = canvas_res.value;

    /* Configure canvas grid/cell dimensions */
    layer->canvas->ops->set_cell_size(
        layer->canvas, (struct yetty_ycore_pixel_size){.width = cell_width, .height = cell_height});
    layer->canvas->ops->set_grid_size(layer->canvas,
                                      (struct yetty_ycore_grid_size){.cols = cols, .rows = rows});

    /* Register scroll/cursor callbacks for propagation to other layers */
    layer->canvas->ops->set_scroll_callback(layer->canvas, on_canvas_scroll, layer);
    layer->canvas->ops->set_cursor_callback(layer->canvas, on_canvas_cursor_set, layer);

    /* Resource set. Both scrolling and overlay layers share one namespace —
   * each layer has its own binder/render-target, so the names cannot collide
   * across layers, and the shader source (ydraw-layer.wgsl) is identical for
   * both modes. The shader references ydraw_* symbols; binder prefixes them
   * with this namespace at compile time. */
    strncpy(layer->rs.namespace, "ydraw", YETTY_YRENDER_NAME_MAX - 1);

    /* Buffer 0: grid staging (cell-to-drawable lookup).
   * children_count is recomputed every frame in get_gpu_resource_set from
   * whatever libraries are active (SDF dispatcher, current font, …). */
    layer->rs.buffer_count = 2;
    strncpy(layer->rs.buffers[0].name, "grid", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(layer->rs.buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    layer->rs.buffers[0].readonly = 1;

    /* Buffer 1: drawable staging (serialized primitives) */
    strncpy(layer->rs.buffers[1].name, "prims", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(layer->rs.buffers[1].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    layer->rs.buffers[1].readonly = 1;

    /* Initialize uniforms - actual values set in get_gpu_resource_set from canvas
   */
    init_uniforms(&layer->rs);

    /* Set initial pixel size for render target */
    layer->rs.pixel_size.width = (float)cols * cell_width;
    layer->rs.pixel_size.height = (float)rows * cell_height;

    yetty_yrender_shader_code_set(&layer->rs.shader, (const char *)layer->shader_code.data,
                                  layer->shader_code.size);

    /* Long-lived streaming decoder for incoming --bin OSC bodies. Reused
   * across every emit, so we don't pay LZ4F_createDecompressionContext
   * per OSC. Same pattern as ymgui-layer. */
    {
        struct yetty_yface_ptr_result yr = yetty_yface_create();
        if (YETTY_IS_ERR(yr)) {
            free(layer->shader_code.data);
            free(layer->sdf_lib_code.data);
            free(layer);
            return YETTY_ERR(yetty_yterm_terminal_layer,
                             "ydraw_layer_create: yetty_yface_create failed", yr);
        }
        layer->yface = yr.value;
    }

    ydebug("ydraw_layer_create: kind=%s, %ux%u grid, %.1fx%.1f cells",
           kind == YETTY_YDRAW_LAYER_KIND_SCROLLING ? "scrolling" : "scene", cols, rows, cell_width,
           cell_height);

    return YETTY_OK(yetty_yterm_terminal_layer, &layer->base);
}

/* Destroy */
static struct yetty_ycore_void_result ydraw_layer_destroy(struct yetty_yrender_terminal_layer *self)
{
    struct yetty_yterm_ydraw_layer *layer = (struct yetty_yterm_ydraw_layer *)self;
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();

    if (layer->yface) {
        yetty_yface_destroy(layer->yface);
    }
    if (layer->canvas) {
        struct yetty_ycore_void_result cr = layer->canvas->ops->destroy(layer->canvas);
        if (YETTY_IS_ERR(cr)) {
            first_err = cr;
        }
    }
    if (layer->saved_canvas) {
        struct yetty_ycore_void_result cr = layer->saved_canvas->ops->destroy(layer->saved_canvas);
        if (YETTY_IS_ERR(cr)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = cr;
            } else {
                yetty_ycore_error_destroy(cr.error);
            }
        }
    }

    free(layer->shader_code.data);
    free(layer->sdf_lib_code.data);
    free(layer->combined_shader);
    free(layer);

    if (YETTY_IS_ERR(first_err)) {
        return YETTY_ERR(yetty_ycore_void, "ydraw_layer_destroy: canvas destroy failed", first_err);
    }
    return YETTY_OK_VOID();
}

/* Process — thin dispatcher over the OSC SM. CLEAR wipes the canvas;
 * BIN / OVERLAY forward straight to the canvas's streaming process_input
 * which pulls prim bytes off the SM (the SM owns b64 + lz4 decoding).
 * YAML is no longer accepted on the wire — yaml is producer-side only. */
static struct yetty_ycore_void_result ydraw_layer_process_input(
    struct yetty_yrender_terminal_layer *self,
    struct yetty_ywire_wire_statemachine *osc_statemachine)
{
    struct yetty_yterm_ydraw_layer *layer = (struct yetty_yterm_ydraw_layer *)self;

    int code = yetty_ywire_wire_statemachine_code(osc_statemachine);
    struct yetty_ycore_void_result r;
    switch (code) {
    case YETTY_OSC_YDRAW_CLEAR:
        r = layer->canvas->ops->clear(layer->canvas);
        break;
    case YETTY_OSC_YDRAW_BIN:
    case YETTY_OSC_YDRAW_OVERLAY:
    case YETTY_OSC_YDRAW_SCENE_BIN:
        /* All three carry a framed YDrawList envelope; the canvas's
         * process_input pulls bytes off the wire-statemachine. The
         * code distinction matters only to yterm's routing (which
         * decides which layer/canvas variant receives the bytes) — by
         * the time we're here the canvas already is the right kind. */
        r = layer->canvas->ops->process_input(layer->canvas, osc_statemachine);
        break;
    default:
        return YETTY_ERR(yetty_ycore_void, "ydraw: unexpected OSC code");
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ydraw: canvas process failed");

    layer->base.dirty = 1;
    if (layer->base.request_render_fn) {
        layer->base.request_render_fn(layer->base.request_render_userdata);
    }
    return YETTY_OK_VOID();
}

/* Resize */
static struct yetty_ycore_void_result ydraw_layer_resize_grid(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_grid_size grid_size)
{
    struct yetty_yterm_ydraw_layer *layer = (struct yetty_yterm_ydraw_layer *)self;

    if (!layer->canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }

    self->grid_size = grid_size;
    layer->canvas->ops->set_grid_size(layer->canvas, grid_size);
    self->dirty = 1;

    ydebug("ydraw_layer_resize_grid: %ux%u", grid_size.cols, grid_size.rows);
    return YETTY_OK_VOID();
}

/* Get GPU resource set */
static struct yetty_yrender_gpu_resource_set_result ydraw_layer_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self)
{
    struct yetty_yterm_ydraw_layer *layer = (struct yetty_yterm_ydraw_layer *)self;

    if (layer->base.dirty || layer->canvas->ops->is_dirty(layer->canvas)) {
        /* Rebuild grid staging */
        layer->canvas->ops->rebuild_grid(layer->canvas);

        const uint32_t *grid_data = layer->canvas->ops->grid_data(layer->canvas);
        uint32_t grid_word_count = layer->canvas->ops->grid_word_count(layer->canvas);

        layer->rs.buffers[0].data = (uint8_t *)grid_data;
        layer->rs.buffers[0].size = grid_word_count * sizeof(uint32_t);
        layer->rs.buffers[0].dirty = 1;

        /* Build drawable staging */
        struct yetty_ydraw_drawable_staging_result ps_r =
            layer->canvas->ops->build_drawable_staging(layer->canvas);
        if (YETTY_IS_ERR(ps_r)) {
            return YETTY_ERR(yetty_yrender_gpu_resource_set,
                             "ydraw_layer_get_gpu_resource_set: build_drawable_staging failed", ps_r);
        }
        const uint32_t *drawable_data = ps_r.value.data;
        uint32_t drawable_word_count = ps_r.value.word_count;

        layer->rs.buffers[1].data = (uint8_t *)drawable_data;
        layer->rs.buffers[1].size = drawable_word_count * sizeof(uint32_t);
        layer->rs.buffers[1].dirty = 1;

        /* Update ALL uniforms from canvas - single source of truth */
        struct yetty_ycore_grid_size gs = layer->canvas->ops->get_grid_size(layer->canvas);
        struct yetty_ycore_pixel_size cs = layer->canvas->ops->get_cell_size(layer->canvas);
        set_grid_size(&layer->rs, (float)gs.cols, (float)gs.rows);
        set_cell_size(&layer->rs, cs.width, cs.height);
        set_rolling_row_0(&layer->rs, layer->canvas->ops->rolling_row_0(layer->canvas));
        uint32_t drawable_count = layer->canvas->ops->drawable_count(layer->canvas);
        set_drawable_count(&layer->rs, drawable_count);

        /* Set pixel size for render target */
        layer->rs.pixel_size.width = (float)gs.cols * cs.width;
        layer->rs.pixel_size.height = (float)gs.rows * cs.height;

        ydebug("ydraw_layer: grid=%ux%u, cell=%.1fx%.1f, prims=%u", gs.cols, gs.rows, cs.width,
               cs.height, drawable_count);

        layer->base.dirty = 0;
    }

    /* Children are merged into the compiled shader by the binder. Order
     * matters here: the SDF library and every canvas font become children
     * in slot order, so the layer's font dispatcher (generated below) can
     * forward `slot` straight to the right `<ns>_…` helper. */
    size_t child_idx = 0;
    layer->rs.children[child_idx++] = &layer->sdf_lib_rs;

    uint32_t font_count = layer->canvas->ops->font_count(layer->canvas);
    /* Cap at the rs.children[] capacity minus the SDF child. */
    if (font_count > YETTY_YRENDER_RS_MAX_CHILDREN - 1) {
        font_count = YETTY_YRENDER_RS_MAX_CHILDREN - 1;
    }
    /* Collect each font's resource set in slot order (slot 0 = default). */
    const struct yetty_ydraw_gpu_resource_set *font_rss[YETTY_YRENDER_RS_MAX_CHILDREN] = {0};
    for (uint32_t s = 0; s < font_count; s++) {
        struct yetty_ydraw_font *f = layer->canvas->ops->get_font_at(layer->canvas, s);
        if (!f || !f->ops || !f->ops->get_gpu_resource_set) {
            continue;
        }
        struct yetty_yrender_gpu_resource_set_result fr = f->ops->get_gpu_resource_set(f);
        if (YETTY_IS_OK(fr)) {
            font_rss[s] = fr.value;
            layer->rs.children[child_idx++] = (struct yetty_ydraw_gpu_resource_set *)fr.value;
        }
    }

    /* (Re)build the per-slot dispatcher block whenever the active font
     * set changes — both growth (new slot) and shrinkage (slot dropped
     * by eviction). The cache's generation counter bumps on alloc AND
     * release, unlike font_count which is a high watermark. */
    uint32_t font_generation = layer->canvas->ops->font_generation(layer->canvas);
    if (font_generation != layer->last_font_generation) {
        size_t cap = layer->shader_code.size + 4096 + (size_t)font_count * 512;
        char *buf = malloc(cap);
        if (buf) {
            int n = snprintf(buf, cap, "// auto-generated font dispatcher (canvas slots 0..%u)\n",
                             font_count);
            size_t off = (n > 0) ? (size_t)n : 0;

            /* font_base_size(slot) */
            off += (size_t)snprintf(buf + off, cap - off,
                                    "fn font_base_size(slot: u32) -> f32 {\n"
                                    "    switch slot {\n");
            for (uint32_t s = 0; s < font_count; s++) {
                if (!font_rss[s]) {
                    continue;
                }
                off += (size_t)snprintf(buf + off, cap - off,
                                        "        case %uu: { return uniforms.%s_base_size; }\n", s,
                                        font_rss[s]->namespace);
            }
            off += (size_t)snprintf(buf + off, cap - off,
                                    "        default: { return 1.0; }\n    }\n}\n");

            /* font_glyph_size(slot, glyph_index) */
            off +=
                (size_t)snprintf(buf + off, cap - off,
                                 "fn font_glyph_size(slot: u32, glyph_index: u32) -> vec2<f32> {\n"
                                 "    switch slot {\n");
            for (uint32_t s = 0; s < font_count; s++) {
                if (!font_rss[s]) {
                    continue;
                }
                off +=
                    (size_t)snprintf(buf + off, cap - off,
                                     "        case %uu: { return %s_glyph_size(glyph_index); }\n",
                                     s, font_rss[s]->namespace);
            }
            off += (size_t)snprintf(buf + off, cap - off,
                                    "        default: { return vec2<f32>(0.0, 0.0); }\n    }\n}\n");

            /* font_glyph_sample(slot, glyph_index, uv, ps) */
            off += (size_t)snprintf(buf + off, cap - off,
                                    "fn font_glyph_sample(slot: u32, glyph_index: u32, "
                                    "uv: vec2<f32>, ps: f32) -> f32 {\n"
                                    "    switch slot {\n");
            for (uint32_t s = 0; s < font_count; s++) {
                if (!font_rss[s]) {
                    continue;
                }
                off += (size_t)snprintf(
                    buf + off, cap - off,
                    "        case %uu: { return %s_glyph_sample(glyph_index, uv, ps); }\n", s,
                    font_rss[s]->namespace);
            }
            off += (size_t)snprintf(buf + off, cap - off,
                                    "        default: { return 0.0; }\n    }\n}\n\n");

            /* Append the static layer source verbatim. */
            if (off + layer->shader_code.size + 1 <= cap) {
                memcpy(buf + off, layer->shader_code.data, layer->shader_code.size);
                off += layer->shader_code.size;
                buf[off] = '\0';

                free(layer->combined_shader);
                layer->combined_shader = buf;
                layer->combined_shader_size = off;
                layer->last_font_count = font_count;
                layer->last_font_generation = font_generation;
                yetty_yrender_shader_code_set(&layer->rs.shader, layer->combined_shader,
                                              layer->combined_shader_size);
                ydebug("ydraw_layer: rebuilt dispatcher for %u fonts gen=%u (%zu bytes)",
                       font_count, font_generation, off);
            } else {
                free(buf);
            }
        }
    }

    /* NOTE: Complex prims (yplot, yimage, yvideo) render via their own pipelines
   * (factory pattern), not as part of ydraw layer shader dispatch. */

    layer->rs.children_count = child_idx;

    return YETTY_OK(yetty_yrender_gpu_resource_set, &layer->rs);
}

/* Keyboard input - ydraw layer doesn't handle keyboard */
static int ydraw_layer_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods)
{
    (void)self;
    (void)key;
    (void)mods;
    return 0; /* Not handled */
}

static int ydraw_layer_on_char(struct yetty_yrender_terminal_layer *self, uint32_t codepoint,
                               int mods)
{
    (void)self;
    (void)codepoint;
    (void)mods;
    return 0; /* Not handled */
}

/* YDraw layer is empty if there are no drawables */
static int ydraw_layer_is_empty(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yterm_ydraw_layer *layer = (const struct yetty_yterm_ydraw_layer *)self;

    if (!layer->canvas) {
        return 1;
    }

    return layer->canvas->ops->drawable_count(layer->canvas) == 0;
}

/* Scroll - called when another layer scrolls */
static struct yetty_ycore_void_result ydraw_layer_scroll(struct yetty_yrender_terminal_layer *self,
                                                         int lines)
{
    struct yetty_yterm_ydraw_layer *layer = (struct yetty_yterm_ydraw_layer *)self;

    ydebug("ydraw_layer_scroll ENTER: lines=%d canvas=%p", lines, (void *)layer->canvas);

    if (!layer->canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (lines <= 0) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result res =
        layer->canvas->ops->scroll_lines(layer->canvas, (uint16_t)lines);
    if (YETTY_IS_ERR(res)) {
        return res;
    }

    layer->base.dirty = 1;

    ydebug("ydraw_layer_scroll EXIT: %d lines scrolled", lines);

    if (layer->base.request_render_fn) {
        layer->base.request_render_fn(layer->base.request_render_userdata);
    }

    return YETTY_OK_VOID();
}

/* Live anchor — canvas-line index of the live viewport top, ignoring any
 * scrollback override. The terminal uses this to convert mouse-wheel deltas
 * into a stable absolute view_top. */
static uint32_t ydraw_layer_get_live_anchor(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yterm_ydraw_layer *layer = (const struct yetty_yterm_ydraw_layer *)self;
    if (!layer->canvas) {
        return 0;
    }
    return layer->canvas->ops->live_rolling_row_0(layer->canvas);
}

/* Pin / release the canvas's viewport for tmux-style scrollback view. */
static struct yetty_ycore_void_result ydraw_layer_set_view_top(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t view_top_total_idx)
{
    struct yetty_yterm_ydraw_layer *layer = (struct yetty_yterm_ydraw_layer *)self;
    if (!layer->canvas) {
        return YETTY_ERR(yetty_ycore_void, "ydraw_layer_set_view_top: NULL canvas");
    }
    struct yetty_ycore_void_result vt =
        layer->canvas->ops->set_view_top(layer->canvas, active ? true : false, view_top_total_idx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vt, "ydraw_layer_set_view_top failed");
    layer->base.dirty = 1;
    if (layer->base.request_render_fn) {
        layer->base.request_render_fn(layer->base.request_render_userdata);
    }
    return YETTY_OK_VOID();
}

/* Alt-screen entry/exit: swap the active canvas with a saved one. The
 * alt canvas is built lazily on first ?1049 entry — most sessions
 * never use it, so paying the cost up-front is wasteful. Each canvas
 * keeps its own drawables, fonts, and rolling-row state; toggling is
 * just a pointer swap (no GPU work, no data copy). */
static struct yetty_ycore_void_result ydraw_layer_set_alt_screen(
    struct yetty_yrender_terminal_layer *self, int active)
{
    struct yetty_yterm_ydraw_layer *layer = (struct yetty_yterm_ydraw_layer *)self;
    int wanted = active ? 1 : 0;
    if (layer->alt_active == wanted) {
        return YETTY_OK_VOID();
    }

    /* Lazy-build the saved-side canvas the first time we toggle. The
   * empty side starts on the alt slot when we enter (alt_active=1),
   * and on the primary slot if for some reason we exit before having
   * entered (shouldn't happen, but cheap to handle). */
    if (!layer->saved_canvas && layer->create_context) {
        struct yetty_ydraw_canvas_ptr_result saved_res =
            (layer->kind == YETTY_YDRAW_LAYER_KIND_SCROLLING)
                ? yetty_ydraw_scrolling_canvas_create(layer->create_context)
                : yetty_ydraw_scene_canvas_create(layer->create_context);
        if (YETTY_IS_ERR(saved_res)) {
            return YETTY_ERR(yetty_ycore_void, "ydraw_layer_set_alt_screen: canvas create failed",
                             saved_res);
        }
        layer->saved_canvas = saved_res.value;
        struct yetty_ycore_void_result r;
        r = layer->saved_canvas->ops->set_cell_size(layer->saved_canvas, layer->base.cell_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "set_alt_screen: set_cell_size failed");
        r = layer->saved_canvas->ops->set_grid_size(layer->saved_canvas, layer->base.grid_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "set_alt_screen: set_grid_size failed");
        r = layer->saved_canvas->ops->set_scroll_callback(layer->saved_canvas, on_canvas_scroll,
                                                          layer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "set_alt_screen: set_scroll_callback failed");
        r = layer->saved_canvas->ops->set_cursor_callback(layer->saved_canvas, on_canvas_cursor_set,
                                                          layer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "set_alt_screen: set_cursor_callback failed");
    }
    if (!layer->saved_canvas) {
        return YETTY_OK_VOID();
    }

    struct yetty_ydraw_canvas *tmp = layer->canvas;
    layer->canvas = layer->saved_canvas;
    layer->saved_canvas = tmp;
    layer->alt_active = wanted;

    layer->base.dirty = 1;
    if (layer->base.request_render_fn) {
        layer->base.request_render_fn(layer->base.request_render_userdata);
    }

    ydebug("ydraw: alt_screen=%d", wanted);
    return YETTY_OK_VOID();
}

/* Set cursor - called when another layer moves cursor */
static struct yetty_ycore_void_result ydraw_layer_set_cursor(
    struct yetty_yrender_terminal_layer *self, int col, int row)
{
    struct yetty_yterm_ydraw_layer *layer = (struct yetty_yterm_ydraw_layer *)self;

    if (!layer->canvas) {
        return YETTY_ERR(yetty_ycore_void, "ydraw_layer_set_cursor: NULL canvas");
    }

    struct yetty_ycore_void_result r = layer->canvas->ops->set_cursor_pos(
        layer->canvas,
        (struct yetty_ycore_grid_cursor_pos){.cols = (uint32_t)col, .rows = (uint32_t)row});
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ydraw_layer_set_cursor failed");
    ydebug("ydraw_layer_set_cursor: col=%d row=%d", col, row);
    return YETTY_OK_VOID();
}

/* Render layer to target - simple prims + complex prims */
static struct yetty_ycore_void_result ydraw_layer_render(struct yetty_yrender_terminal_layer *self,
                                                         struct yetty_ydraw_target *target)
{
    struct yetty_yterm_ydraw_layer *layer = (struct yetty_yterm_ydraw_layer *)self;

    /* Render simple prims via render_layer */
    struct yetty_ycore_void_result res = target->ops->render_layer(target, self);
    if (!YETTY_IS_OK(res)) {
        return res;
    }

    /* Render complex prims */
    if (!layer->canvas) {
        return YETTY_OK_VOID();
    }

    uint32_t count = layer->canvas->ops->figure_count(layer->canvas);
    if (count == 0) {
        return YETTY_OK_VOID();
    }

    uint32_t row0 = layer->canvas->ops->rolling_row_0(layer->canvas);
    struct yetty_ycore_pixel_size cell_size = layer->canvas->ops->get_cell_size(layer->canvas);

    for (uint32_t i = 0; i < count; i++) {
        struct yetty_ydraw_figure_instance *inst = layer->canvas->ops->get_figure(layer->canvas, i);
        if (!inst || !inst->render) {
            continue;
        }

        /* Calculate screen position from bounds and scroll offset */
        float y_offset = (float)((int32_t)inst->rolling_row - (int32_t)row0) * cell_size.height;
        float screen_x = inst->bounds.min.x;
        float screen_y = inst->bounds.min.y + y_offset;

        res = inst->render(inst, target, screen_x, screen_y);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res,
                            "ydraw_layer_render: complex prim render failed");
    }

    return YETTY_OK_VOID();
}

/* Selection on the ydraw layer is row-only — the column part of the
 * terminal-wide (anchor, head) pair has no meaning on rich content. We
 * collapse to [min_row, max_row] and stash it for the text extractor.
 * Highlight rendering is intentionally not wired up yet; the text-layer
 * already shows the user where their selection band is. */
static struct yetty_ycore_void_result ydraw_layer_set_selection(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t anchor_row, uint32_t anchor_col,
    uint32_t head_row, uint32_t head_col)
{
    struct yetty_yterm_ydraw_layer *layer =
        container_of(self, struct yetty_yterm_ydraw_layer, base);
    (void)anchor_col;
    (void)head_col;
    layer->sel_active = active;
    if (anchor_row <= head_row) {
        layer->sel_min_row = anchor_row;
        layer->sel_max_row = head_row;
    } else {
        layer->sel_min_row = head_row;
        layer->sel_max_row = anchor_row;
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Selection text extraction — reconstruct UTF-8 from ydraw glyph prims
 *
 * The ydraw canvas stores text as flyweight GLYPH drawables — each one
 * is a single glyph at a pixel position, with the font atlas index (and
 * canvas font slot) packed in. To produce plain text for the clipboard we:
 *   1. walk every glyph in the canvas via for_each_glyph
 *   2. keep the ones whose y lies inside the selected row band
 *   3. sort by (y, x) — reading order
 *   4. group consecutive glyphs that share the same canvas row, separated
 *      by '\n'
 *   5. reverse each glyph's atlas index through its font's get_codepoint
 *      and emit UTF-8
 * The ydraw font's get_codepoint is the inverse of its forward
 * get_glyph_index map — same trick we use on the text-layer's font.
 *===========================================================================*/

static void ydraw_layer_collect_visitor(const struct yetty_ydraw_glyph_view *gv, void *user)
{
    struct {
        struct yetty_ydraw_glyph_view *arr;
        size_t count, cap;
    } *ctx = user;
    if (ctx->count >= ctx->cap) {
        size_t newcap = ctx->cap ? ctx->cap * 2 : 64;
        struct yetty_ydraw_glyph_view *p = realloc(ctx->arr, newcap * sizeof(*ctx->arr));
        if (!p) {
            return;
        }
        ctx->arr = p;
        ctx->cap = newcap;
    }
    ctx->arr[ctx->count++] = *gv;
}

static int ydraw_layer_glyph_view_compare(const void *a, const void *b)
{
    const struct yetty_ydraw_glyph_view *x = a;
    const struct yetty_ydraw_glyph_view *y = b;
    if (x->y < y->y) {
        return -1;
    }
    if (x->y > y->y) {
        return 1;
    }
    if (x->x < y->x) {
        return -1;
    }
    if (x->x > y->x) {
        return 1;
    }
    return 0;
}

/* Encode one codepoint as UTF-8 into a 4-byte buffer. Returns byte count.
 * Same logic as the text-layer copy — small enough that the duplication
 * is cheaper than a shared helper across module boundaries. */
static size_t ydraw_layer_cp_to_utf8(uint32_t cp, uint8_t out[4])
{
    if (cp < 0x80) {
        out[0] = (uint8_t)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (uint8_t)(0xC0 | (cp >> 6));
        out[1] = (uint8_t)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (uint8_t)(0xE0 | (cp >> 12));
        out[1] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (uint8_t)(0x80 | (cp & 0x3F));
        return 3;
    }
    if (cp <= 0x10FFFF) {
        out[0] = (uint8_t)(0xF0 | (cp >> 18));
        out[1] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (uint8_t)(0x80 | (cp & 0x3F));
        return 4;
    }
    out[0] = 0xEF;
    out[1] = 0xBF;
    out[2] = 0xBD;
    return 3;
}

static struct yetty_ycore_void_result ydraw_layer_get_selection_text(
    const struct yetty_yrender_terminal_layer *self, struct yetty_ycore_buffer *out)
{
    struct yetty_yterm_ydraw_layer *layer = container_of(
        (struct yetty_yrender_terminal_layer *)self, struct yetty_yterm_ydraw_layer, base);

    if (!layer->sel_active || !layer->canvas || !out) {
        return YETTY_OK_VOID();
    }
    float cell_h = self->cell_size.height;
    if (cell_h <= 0.0f) {
        return YETTY_OK_VOID();
    }

    /* Visible rows → absolute pixel y band. The canvas stores glyphs in
     * absolute canvas-y coords; the visible viewport starts at
     * rolling_row_0 * cell_h. */
    uint32_t row0 = layer->canvas->ops->rolling_row_0(layer->canvas);
    float sel_top = (float)(row0 + layer->sel_min_row) * cell_h;
    float sel_bot = (float)(row0 + layer->sel_max_row + 1) * cell_h;

    /* Collect every glyph in the canvas, then filter + sort. */
    struct {
        struct yetty_ydraw_glyph_view *arr;
        size_t count, cap;
    } ctx = {NULL, 0, 0};
    {
        struct yetty_ycore_void_result gr =
            layer->canvas->ops->for_each_glyph(layer->canvas, ydraw_layer_collect_visitor, &ctx);
        if (YETTY_IS_ERR(gr)) {
            free(ctx.arr);
            return YETTY_ERR(yetty_ycore_void, "ydraw_layer: for_each_glyph failed", gr);
        }
    }
    if (ctx.count == 0) {
        free(ctx.arr);
        return YETTY_OK_VOID();
    }

    /* In-place filter: keep glyphs whose vertical extent intersects the
     * selection. We approximate a glyph's extent as [y, y + cell_h]; the
     * cell height is a tight upper bound for ydraw glyphs at the default
     * font size and is good enough for a row-band test. */
    size_t kept = 0;
    for (size_t i = 0; i < ctx.count; i++) {
        float gy = ctx.arr[i].y;
        if (gy >= sel_bot || gy + cell_h <= sel_top) {
            continue;
        }
        ctx.arr[kept++] = ctx.arr[i];
    }
    ctx.count = kept;
    if (ctx.count == 0) {
        free(ctx.arr);
        return YETTY_OK_VOID();
    }

    qsort(ctx.arr, ctx.count, sizeof(ctx.arr[0]), ydraw_layer_glyph_view_compare);

    /* Walk in reading order; insert '\n' between glyphs that belong to
     * different canvas rows. Row is computed from y to be robust against
     * glyphs with the same y but tiny float jitter. */
    struct yetty_ycore_void_result rret = YETTY_OK_VOID();
    int last_row = -1;
    for (size_t i = 0; i < ctx.count; i++) {
        int gv_row = (int)(ctx.arr[i].y / cell_h);
        if (last_row >= 0 && gv_row != last_row) {
            rret = yetty_ycore_buffer_write(out, "\n", 1);
            if (!YETTY_IS_OK(rret)) {
                goto done;
            }
        }
        last_row = gv_row;

        uint32_t slot = ctx.arr[i].font_slot >= 0 ? (uint32_t)ctx.arr[i].font_slot : 0;
        struct yetty_ydraw_font *font = layer->canvas->ops->get_font_at(layer->canvas, slot);
        uint32_t cp = 0xFFFD; /* fall back to U+FFFD on any failure */
        if (font && font->ops && font->ops->get_codepoint) {
            struct uint32_result cr = font->ops->get_codepoint(font, ctx.arr[i].glyph_idx);
            if (YETTY_IS_OK(cr)) {
                cp = cr.value;
            }
        }
        uint8_t utf8[4];
        size_t n = ydraw_layer_cp_to_utf8(cp, utf8);
        rret = yetty_ycore_buffer_write(out, utf8, n);
        if (!YETTY_IS_OK(rret)) {
            goto done;
        }
    }

done:
    free(ctx.arr);
    return rret;
}
