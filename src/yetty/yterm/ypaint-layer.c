#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/util.h>
#include <yetty/yfont/font.h>
#include <yetty/yface/yface.h>
#include <yetty/ypaint-core/buffer.h>
#include <yetty/ypaint-core/complex-prim-types.h>
#include <yetty/ypaint-factory/complex-prim-factory.h>
#include <yetty/ypaint/core/ypaint-canvas.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ypaint-yaml/ypaint-yaml.h>
#include <yetty/yterm/osc-args.h>
#include <yetty/yterm/pty-reader.h> /* YETTY_OSC_YPAINT_* */
#include <yetty/yterm/ypaint-layer.h>
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
static inline void set_grid_size(struct yetty_ypaint_core_gpu_resource_set *rs, float cols,
                                 float rows)
{
    rs->uniforms[U_GRID_SIZE].vec2[0] = cols;
    rs->uniforms[U_GRID_SIZE].vec2[1] = rows;
}
static inline void set_cell_size(struct yetty_ypaint_core_gpu_resource_set *rs, float w, float h)
{
    rs->uniforms[U_CELL_SIZE].vec2[0] = w;
    rs->uniforms[U_CELL_SIZE].vec2[1] = h;
}
static inline void set_rolling_row_0(struct yetty_ypaint_core_gpu_resource_set *rs,
                                     uint32_t row_origin)
{
    rs->uniforms[U_ROLLING_ROW_0].u32 = row_origin;
}
static inline void set_prim_count(struct yetty_ypaint_core_gpu_resource_set *rs, uint32_t count)
{
    rs->uniforms[U_PRIM_COUNT].u32 = count;
}
static inline void set_visual_zoom(struct yetty_ypaint_core_gpu_resource_set *rs, float scale,
                                   float off_x, float off_y)
{
    rs->uniforms[U_VZ_SCALE].f32 = scale;
    rs->uniforms[U_VZ_OFF].vec2[0] = off_x;
    rs->uniforms[U_VZ_OFF].vec2[1] = off_y;
}
static inline void set_cell_zoom(struct yetty_ypaint_core_gpu_resource_set *rs, float scale,
                                 float off_x, float off_y)
{
    rs->uniforms[U_CZ_SCALE].f32 = scale;
    rs->uniforms[U_CZ_OFF].vec2[0] = off_x;
    rs->uniforms[U_CZ_OFF].vec2[1] = off_y;
}

/* Init uniforms */
static void init_uniforms(struct yetty_ypaint_core_gpu_resource_set *rs)
{
    rs->uniform_count = U_COUNT;

    rs->uniforms[U_GRID_SIZE] =
        (struct yetty_yrender_uniform){"ypaint_grid_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_CELL_SIZE] =
        (struct yetty_yrender_uniform){"ypaint_cell_size", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_ROLLING_ROW_0] =
        (struct yetty_yrender_uniform){"ypaint_rolling_row_0", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_PRIM_COUNT] =
        (struct yetty_yrender_uniform){"ypaint_prim_count", YETTY_YRENDER_UNIFORM_U32};
    rs->uniforms[U_VZ_SCALE] =
        (struct yetty_yrender_uniform){"ypaint_visual_zoom_scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_VZ_OFF] =
        (struct yetty_yrender_uniform){"ypaint_visual_zoom_off", YETTY_YRENDER_UNIFORM_VEC2};
    rs->uniforms[U_CZ_SCALE] =
        (struct yetty_yrender_uniform){"ypaint_cell_zoom_scale", YETTY_YRENDER_UNIFORM_F32};
    rs->uniforms[U_CZ_OFF] =
        (struct yetty_yrender_uniform){"ypaint_cell_zoom_off", YETTY_YRENDER_UNIFORM_VEC2};

    set_rolling_row_0(rs, 0);
    set_prim_count(rs, 0);
    set_visual_zoom(rs, 1.0f, 0.0f, 0.0f);
    set_cell_zoom(rs, 1.0f, 0.0f, 0.0f);
}

/* YPaint layer - embeds base as first member */
struct yetty_yterm_ypaint_layer {
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
    struct yetty_ypaint_core_gpu_resource_set sdf_lib_rs;
    struct yetty_ypaint_canvas *canvas;
    int scrolling_mode;
    struct yetty_ypaint_core_gpu_resource_set rs;
    struct yetty_ycore_buffer shader_code;

    /* Combined layer shader: generated `font_glyph_*` dispatcher block
     * (one switch case per canvas font) prepended to the static
     * shader_code above. Regenerated on the fly whenever the canvas's
     * font count changes. The pipeline keys recompiles on the layer's
     * shader hash, so changing this triggers a recompile. */
    char *combined_shader;
    size_t combined_shader_size;
    uint32_t last_font_count; /* canvas font count baked into combined */

    /* Staging buffers - point to canvas data */
    uint8_t *grid_staging;
    size_t grid_staging_size;
    uint8_t *prim_staging;
    size_t prim_staging_size;

    /* Streaming OSC decoder (b64 → LZ4F → in_buf) — one per layer, lives
   * for the layer's lifetime. Same pattern as ymgui-layer. */
    struct yetty_yface *yface;

    /* Cached at create — needed to lazily build the alt-screen canvas
   * on first ?1049 toggle. */
    const struct yetty_context *create_context;

    /* Alt-screen state. The active canvas is `canvas` above; the saved
   * counterpart (primary while in alt, alt while in primary) lives
   * here. Toggle via ypaint_layer_set_alt_screen swaps the two. */
    int alt_active;
    struct yetty_ypaint_canvas *saved_canvas;
};

/* Forward declarations */
static struct yetty_ycore_void_result ypaint_layer_destroy(
    struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result ypaint_layer_write(struct yetty_yrender_terminal_layer *self,
                                                         int osc_code, const char *data,
                                                         size_t len);
static struct yetty_ycore_void_result ypaint_layer_resize_grid(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_grid_size grid_size);
static struct yetty_yrender_gpu_resource_set_result ypaint_layer_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self);
static int ypaint_layer_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods);
static int ypaint_layer_on_char(struct yetty_yrender_terminal_layer *self, uint32_t codepoint,
                                int mods);
static int ypaint_layer_is_empty(const struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result ypaint_layer_scroll(struct yetty_yrender_terminal_layer *self,
                                                          int lines);
static struct yetty_ycore_void_result ypaint_layer_set_cursor(
    struct yetty_yrender_terminal_layer *self, int col, int row);
static struct yetty_ycore_void_result ypaint_layer_render(struct yetty_yrender_terminal_layer *self,
                                                          struct yetty_ypaint_core_target *target);
static uint32_t ypaint_layer_get_live_anchor(const struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result ypaint_layer_set_view_top(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t view_top_total_idx);
static struct yetty_ycore_void_result ypaint_layer_set_alt_screen(
    struct yetty_yrender_terminal_layer *self, int active);

/* Canvas scroll callback - propagate to other layers */
static struct yetty_ycore_void_result on_canvas_scroll(struct yetty_ycore_void_result *user_data,
                                                       uint16_t num_lines)
{
    struct yetty_yterm_ypaint_layer *layer = (struct yetty_yterm_ypaint_layer *)user_data;
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
    if (YETTY_IS_ERR(res)) {
        yerror("on_canvas_scroll: scroll_fn failed: %s", res.error.msg);
        return res;
    }
    ydebug("on_canvas_scroll EXIT: num_lines=%u", num_lines);
    return YETTY_OK_VOID();
}

/* Canvas cursor callback - propagate to other layers */
static struct yetty_ycore_void_result on_canvas_cursor_set(
    struct yetty_ycore_void_result *user_data, uint16_t new_row)
{
    struct yetty_yterm_ypaint_layer *layer = (struct yetty_yterm_ypaint_layer *)user_data;
    ydebug("on_canvas_cursor_set ENTER: new_row=%u", new_row);
    if (!layer->base.cursor_fn) {
        yerror("on_canvas_cursor_set: cursor_fn is NULL");
        return YETTY_ERR(yetty_ycore_void, "cursor_fn is NULL");
    }
    layer->base.cursor_fn(&layer->base,
                          (struct yetty_ycore_grid_cursor_pos){.cols = 0, .rows = new_row},
                          layer->base.cursor_userdata);
    ydebug("on_canvas_cursor_set EXIT: new_row=%u", new_row);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ypaint_layer_set_cell_size(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_pixel_size cell_size)
{
    struct yetty_yterm_ypaint_layer *layer = (struct yetty_yterm_ypaint_layer *)self;
    if (cell_size.width <= 0.0f || cell_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "invalid cell size");
    }
    if (!layer->canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }

    self->cell_size = cell_size;
    self->dirty = 1;

    /* Don't touch the canvas — keeping canvas cell_size/grid_size constant
     * preserves the existing primitive buckets (ypaint prims store absolute
     * pixel coords in the same frame the canvas was built in). The zoom is
     * achieved purely via a shader uniform transform, same mechanism as the
     * non-intrusive visual zoom, semantically separate (own uniform pair). */
    float base_h = layer->initial_cell_size.height;
    float cz = (base_h > 0.0f) ? (cell_size.height / base_h) : 1.0f;
    set_cell_zoom(&layer->rs, cz, 0.0f, 0.0f);

    /* Fan out to complex-prim factories so yplot and friends apply the
     * same transform in their own shaders. */
    struct yetty_ypaint_core_complex_prim_factory *f =
        yetty_ypaint_canvas_get_complex_prim_factory(layer->canvas);
    yetty_ypaint_core_complex_prim_factory_set_cell_zoom(f, cz, 0.0f, 0.0f);

    ydebug("ypaint_layer_set_cell_size: %.1fx%.1f cell_zoom=%.3f", cell_size.width,
           cell_size.height, cz);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ypaint_layer_set_visual_zoom(
    struct yetty_yrender_terminal_layer *self, float scale, float off_x, float off_y)
{
    struct yetty_yterm_ypaint_layer *layer = (struct yetty_yterm_ypaint_layer *)self;
    set_visual_zoom(&layer->rs, scale, off_x, off_y);
    self->dirty = 1;

    /* Complex primitives (yplot / yimage / …) render through their own
     * pipelines with their own fragment shaders — they don't go through the
     * ypaint-layer shader. Push the zoom into every concrete factory's shared
     * uniforms so each type's shader can apply the same transform. */
    if (layer->canvas) {
        struct yetty_ypaint_core_complex_prim_factory *f =
            yetty_ypaint_canvas_get_complex_prim_factory(layer->canvas);
        yetty_ypaint_core_complex_prim_factory_set_visual_zoom(f, scale, off_x, off_y);
    }
    return YETTY_OK_VOID();
}

/* Ops */
static const struct yetty_yterm_terminal_layer_ops ypaint_layer_ops = {
    .destroy = ypaint_layer_destroy,
    .write = ypaint_layer_write,
    .resize_grid = ypaint_layer_resize_grid,
    .set_cell_size = ypaint_layer_set_cell_size,
    .set_visual_zoom = ypaint_layer_set_visual_zoom,
    .get_gpu_resource_set = ypaint_layer_get_gpu_resource_set,
    .render = ypaint_layer_render,
    .is_empty = ypaint_layer_is_empty,
    .on_key = ypaint_layer_on_key,
    .on_char = ypaint_layer_on_char,
    .scroll = ypaint_layer_scroll,
    .set_cursor = ypaint_layer_set_cursor,
    .get_live_anchor = ypaint_layer_get_live_anchor,
    .set_view_top = ypaint_layer_set_view_top,
    .set_alt_screen = ypaint_layer_set_alt_screen,
};

/* Create */
struct yetty_yterm_terminal_layer_result yetty_yterm_ypaint_layer_create(
    uint32_t cols, uint32_t rows, float cell_width, float cell_height, int scrolling_mode,
    const struct yetty_context *context, yetty_yterm_request_render_fn request_render_fn,
    void *request_render_userdata, yetty_yterm_scroll_fn scroll_fn, void *scroll_userdata,
    yetty_yterm_cursor_fn cursor_fn, void *cursor_userdata)
{
    struct yetty_yterm_ypaint_layer *layer;

    /* Load ypaint-layer shader from file */
    struct yetty_yconfig_config *config = context->app_context.config;
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char shader_path[512];
    char sdf_lib_path[512];
    snprintf(shader_path, sizeof(shader_path), "%s/ypaint-layer.wgsl", shaders_dir);
    /* Generated by src/yetty/ysdf/gen-sdf-code.py — single source of truth for
   * SDF dispatch. Attached below as a child rs; the binder merges its shader
   * source into ypaint-layer's compile. */
    snprintf(sdf_lib_path, sizeof(sdf_lib_path), "%s/ysdf.gen.wgsl", shaders_dir);

    struct yetty_ycore_buffer_result shader_res = yetty_ycore_read_file(shader_path);
    if (YETTY_IS_ERR(shader_res)) {
        return YETTY_ERR(yetty_yterm_terminal_layer, shader_res.error.msg);
    }
    struct yetty_ycore_buffer_result sdf_lib_res = yetty_ycore_read_file(sdf_lib_path);
    if (YETTY_IS_ERR(sdf_lib_res)) {
        free(shader_res.value.data);
        return YETTY_ERR(yetty_yterm_terminal_layer, sdf_lib_res.error.msg);
    }

    layer = calloc(1, sizeof(struct yetty_yterm_ypaint_layer));
    if (!layer) {
        free(shader_res.value.data);
        free(sdf_lib_res.value.data);
        return YETTY_ERR(yetty_yterm_terminal_layer, "failed to allocate ypaint layer");
    }
    layer->shader_code = shader_res.value;
    layer->sdf_lib_code = sdf_lib_res.value;
    strncpy(layer->sdf_lib_rs.namespace, "ysdf_lib", YETTY_YRENDER_NAME_MAX - 1);
    yetty_yrender_shader_code_set(&layer->sdf_lib_rs.shader, (const char *)layer->sdf_lib_code.data,
                                  layer->sdf_lib_code.size);

    layer->base.ops = &ypaint_layer_ops;
    layer->base.grid_size.cols = cols;
    layer->base.grid_size.rows = rows;
    layer->base.cell_size.width = cell_width;
    layer->base.cell_size.height = cell_height;
    layer->initial_cell_size.width = cell_width;
    layer->initial_cell_size.height = cell_height;
    layer->base.dirty = 0;
    layer->base.pty_write_fn = NULL; /* ypaint layer doesn't write to PTY */
    layer->base.pty_write_userdata = NULL;
    layer->base.request_render_fn = request_render_fn;
    layer->base.request_render_userdata = request_render_userdata;
    layer->base.scroll_fn = scroll_fn;
    layer->base.scroll_userdata = scroll_userdata;
    layer->base.cursor_fn = cursor_fn;
    layer->base.cursor_userdata = cursor_userdata;

    layer->scrolling_mode = scrolling_mode;
    layer->create_context = context;

    /* Create canvas (passes context for default font creation) */
    if (!context) {
        free(layer);
        return YETTY_ERR(yetty_yterm_terminal_layer, "context is NULL");
    }
    struct yetty_ypaint_canvas_ptr_result canvas_res =
        yetty_ypaint_canvas_create(scrolling_mode ? true : false, context);
    if (YETTY_IS_ERR(canvas_res)) {
        free(layer);
        return YETTY_ERR(yetty_yterm_terminal_layer, "ypaint-layer: canvas create failed",
                         canvas_res);
    }
    layer->canvas = canvas_res.value;

    /* Configure canvas grid/cell dimensions */
    yetty_ypaint_canvas_set_cell_size(
        layer->canvas, (struct yetty_ycore_pixel_size){.width = cell_width, .height = cell_height});
    yetty_ypaint_canvas_set_grid_size(layer->canvas,
                                      (struct yetty_ycore_grid_size){.cols = cols, .rows = rows});

    /* Register scroll/cursor callbacks for propagation to other layers */
    yetty_ypaint_canvas_set_scroll_callback(layer->canvas, on_canvas_scroll,
                                            (struct yetty_ycore_void_result *)layer);
    yetty_ypaint_canvas_set_cursor_callback(layer->canvas, on_canvas_cursor_set,
                                            (struct yetty_ycore_void_result *)layer);

    /* Resource set. Both scrolling and overlay layers share one namespace —
   * each layer has its own binder/render-target, so the names cannot collide
   * across layers, and the shader source (ypaint-layer.wgsl) is identical for
   * both modes. The shader references ypaint_* symbols; binder prefixes them
   * with this namespace at compile time. */
    strncpy(layer->rs.namespace, "ypaint", YETTY_YRENDER_NAME_MAX - 1);

    /* Buffer 0: grid staging (cell-to-primitive lookup).
   * children_count is recomputed every frame in get_gpu_resource_set from
   * whatever libraries are active (SDF dispatcher, current font, …). */
    layer->rs.buffer_count = 2;
    strncpy(layer->rs.buffers[0].name, "grid", YETTY_YRENDER_NAME_MAX - 1);
    strncpy(layer->rs.buffers[0].wgsl_type, "array<u32>", YETTY_YRENDER_WGSL_TYPE_MAX - 1);
    layer->rs.buffers[0].readonly = 1;

    /* Buffer 1: primitive staging (serialized primitives) */
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
            return YETTY_ERR(yetty_yterm_terminal_layer, yr.error.msg);
        }
        layer->yface = yr.value;
    }

    ydebug("ypaint_layer_create: %s mode, %ux%u grid, %.1fx%.1f cells",
           scrolling_mode ? "scrolling" : "overlay", cols, rows, cell_width, cell_height);

    return YETTY_OK(yetty_yterm_terminal_layer, &layer->base);
}

/* Destroy */
static struct yetty_ycore_void_result ypaint_layer_destroy(
    struct yetty_yrender_terminal_layer *self)
{
    struct yetty_yterm_ypaint_layer *layer = (struct yetty_yterm_ypaint_layer *)self;
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();

    if (layer->yface) {
        yetty_yface_destroy(layer->yface);
    }
    if (layer->canvas) {
        struct yetty_ycore_void_result cr = yetty_ypaint_canvas_destroy(layer->canvas);
        if (YETTY_IS_ERR(cr)) {
            first_err = cr;
        }
    }
    if (layer->saved_canvas) {
        struct yetty_ycore_void_result cr = yetty_ypaint_canvas_destroy(layer->saved_canvas);
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
        return YETTY_ERR(yetty_ycore_void, "ypaint_layer_destroy: canvas destroy failed",
                         first_err);
    }
    return YETTY_OK_VOID();
}

/* Split "<b64-args>;<b64-payload>" into the two slots. Returns 0 on
 * success. Either slot may be empty (zero-length pointer is fine).
 * On failure (no separator), `*payload` and `*payload_len` are zeroed. */
static int split_args_payload(const char *data, size_t len, const char **args, size_t *args_len,
                              const char **payload, size_t *payload_len)
{
    *args = data;
    *args_len = 0;
    *payload = NULL;
    *payload_len = 0;
    for (size_t i = 0; i < len; i++) {
        if (data[i] == ';') {
            *args_len = i;
            *payload = data + i + 1;
            *payload_len = len - i - 1;
            return 0;
        }
    }
    return -1;
}

static struct yetty_ycore_void_result ypaint_handle_clear(struct yetty_yterm_ypaint_layer *layer)
{
    ydebug("ypaint_layer_write: clearing canvas");
    yetty_ypaint_canvas_clear(layer->canvas);
    layer->base.dirty = 1;
    if (layer->base.request_render_fn) {
        layer->base.request_render_fn(layer->base.request_render_userdata);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result ypaint_handle_yaml(struct yetty_yterm_ypaint_layer *layer,
                                                         const char *b64, size_t b64_len)
{
    if (!b64 || b64_len == 0) {
        return YETTY_ERR(yetty_ycore_void, "ypaint: empty yaml payload");
    }

    /* Plain b64-decode (no LZ4) into a NUL-terminated text buffer. */
    size_t decoded_cap = b64_len;
    char *decoded = malloc(decoded_cap + 1);
    if (!decoded) {
        return YETTY_ERR(yetty_ycore_void, "malloc failed");
    }
    size_t decoded_len = yetty_ycore_base64_decode(b64, b64_len, decoded, decoded_cap);
    decoded[decoded_len] = '\0';

    struct yetty_ypaint_core_buffer_result res = yetty_ypaint_yaml_parse(decoded, decoded_len);
    free(decoded);
    if (YETTY_IS_ERR(res)) {
        return YETTY_ERR(yetty_ycore_void, res.error.msg);
    }

    struct yetty_ycore_void_result add_res =
        yetty_ypaint_canvas_add_buffer(layer->canvas, res.value);
    yetty_ypaint_core_buffer_destroy(res.value);
    return add_res;
}

static struct yetty_ycore_void_result ypaint_handle_bin(struct yetty_yterm_ypaint_layer *layer,
                                                        const char *b64_args, size_t b64_args_len,
                                                        const char *b64_payload,
                                                        size_t b64_payload_len)
{
    /* Decode args slot first to discover compressed flag. */
    int compressed = 0;
    if (b64_args_len > 0) {
        /* Args is a b64-encoded yetty_yface_bin_meta (32 bytes raw → ~44 b64
     * chars). Decode in-place into a stack buffer. */
        char meta_raw[64];
        size_t meta_raw_len =
            yetty_ycore_base64_decode(b64_args, b64_args_len, meta_raw, sizeof(meta_raw));
        if (meta_raw_len >= sizeof(struct yetty_yface_bin_meta)) {
            const struct yetty_yface_bin_meta *m = (const struct yetty_yface_bin_meta *)meta_raw;
            if (m->magic == YETTY_YFACE_BIN_MAGIC) {
                compressed = (m->compressed != 0);
            }
        }
    }

    ydebug("ypaint_layer_write: bin compressed=%d payload_len=%zu", compressed, b64_payload_len);

    struct yetty_ycore_void_result r = yetty_yface_start_read(layer->yface, compressed);
    if (YETTY_IS_ERR(r)) {
        return r;
    }
    r = yetty_yface_feed(layer->yface, b64_payload, b64_payload_len);
    if (YETTY_IS_ERR(r)) {
        yetty_yface_finish_read(layer->yface);
        return r;
    }
    r = yetty_yface_finish_read(layer->yface);
    if (YETTY_IS_ERR(r)) {
        return r;
    }

    struct yetty_ycore_buffer *in = yetty_yface_in_buf(layer->yface);
    struct yetty_ypaint_core_buffer_result res =
        yetty_ypaint_core_buffer_create_from_bytes(in->data, in->size);
    if (YETTY_IS_ERR(res)) {
        return YETTY_ERR(yetty_ycore_void, res.error.msg);
    }

    struct yetty_ycore_void_result add_res =
        yetty_ypaint_canvas_add_buffer(layer->canvas, res.value);
    yetty_ypaint_core_buffer_destroy(res.value);
    return add_res;
}

/* Write — receives the OSC body after the leading "<code>;" has been
 * stripped by pty-reader. Body shape (uniform): "<b64-args>;<b64-payload>".
 * Dispatch is by `osc_code` — no verb parsing. */
static struct yetty_ycore_void_result ypaint_layer_write(struct yetty_yrender_terminal_layer *self,
                                                         int osc_code, const char *data, size_t len)
{
    struct yetty_yterm_ypaint_layer *layer = (struct yetty_yterm_ypaint_layer *)self;

    /* clear has an empty body — short-circuit before splitting. */
    if (osc_code == YETTY_OSC_YPAINT_CLEAR) {
        return ypaint_handle_clear(layer);
    }

    const char *args = NULL, *payload = NULL;
    size_t args_len = 0, payload_len = 0;
    if (!data || len == 0 ||
        split_args_payload(data, len, &args, &args_len, &payload, &payload_len) < 0) {
        return YETTY_ERR(yetty_ycore_void, "ypaint: malformed body (need ;)");
    }

    struct yetty_ycore_void_result r;
    switch (osc_code) {
    case YETTY_OSC_YPAINT_BIN:
        r = ypaint_handle_bin(layer, args, args_len, payload, payload_len);
        break;
    case YETTY_OSC_YPAINT_YAML:
        r = ypaint_handle_yaml(layer, payload, payload_len);
        break;
    case YETTY_OSC_YPAINT_OVERLAY:
        /* Same shape as bin for now — overlay layer registration uses a
     * different terminal-layer instance, so this branch is reached
     * only if both register for the same code. */
        r = ypaint_handle_bin(layer, args, args_len, payload, payload_len);
        break;
    default:
        return YETTY_ERR(yetty_ycore_void, "ypaint: unexpected OSC code");
    }
    if (YETTY_IS_ERR(r)) {
        return r;
    }

    layer->base.dirty = 1;
    if (layer->base.request_render_fn) {
        layer->base.request_render_fn(layer->base.request_render_userdata);
    }
    return YETTY_OK_VOID();
}

/* Resize */
static struct yetty_ycore_void_result ypaint_layer_resize_grid(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_grid_size grid_size)
{
    struct yetty_yterm_ypaint_layer *layer = (struct yetty_yterm_ypaint_layer *)self;

    if (!layer->canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }

    self->grid_size = grid_size;
    yetty_ypaint_canvas_set_grid_size(layer->canvas, grid_size);
    self->dirty = 1;

    ydebug("ypaint_layer_resize_grid: %ux%u", grid_size.cols, grid_size.rows);
    return YETTY_OK_VOID();
}

/* Get GPU resource set */
static struct yetty_yrender_gpu_resource_set_result ypaint_layer_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self)
{
    struct yetty_yterm_ypaint_layer *layer = (struct yetty_yterm_ypaint_layer *)self;

    if (layer->base.dirty || yetty_ypaint_canvas_is_dirty(layer->canvas)) {
        /* Rebuild grid staging */
        yetty_ypaint_canvas_rebuild_grid(layer->canvas);

        const uint32_t *grid_data = yetty_ypaint_canvas_grid_data(layer->canvas);
        uint32_t grid_word_count = yetty_ypaint_canvas_grid_word_count(layer->canvas);

        layer->rs.buffers[0].data = (uint8_t *)grid_data;
        layer->rs.buffers[0].size = grid_word_count * sizeof(uint32_t);
        layer->rs.buffers[0].dirty = 1;

        /* Build primitive staging */
        struct yetty_ypaint_prim_staging_result ps_r =
            yetty_ypaint_canvas_build_prim_staging(layer->canvas);
        const uint32_t *prim_data = NULL;
        uint32_t prim_word_count = 0;
        if (YETTY_IS_OK(ps_r)) {
            prim_data = ps_r.value.data;
            prim_word_count = ps_r.value.word_count;
        } else {
            yetty_ycore_error_destroy(ps_r.error);
        }

        layer->rs.buffers[1].data = (uint8_t *)prim_data;
        layer->rs.buffers[1].size = prim_word_count * sizeof(uint32_t);
        layer->rs.buffers[1].dirty = 1;

        /* Update ALL uniforms from canvas - single source of truth */
        struct yetty_ycore_grid_size gs = yetty_ypaint_canvas_get_grid_size(layer->canvas);
        struct yetty_ycore_pixel_size cs = yetty_ypaint_canvas_cell_get_pixel_size(layer->canvas);
        set_grid_size(&layer->rs, (float)gs.cols, (float)gs.rows);
        set_cell_size(&layer->rs, cs.width, cs.height);
        set_rolling_row_0(&layer->rs, yetty_ypaint_canvas_rolling_row_0(layer->canvas));
        uint32_t prim_count = yetty_ypaint_canvas_primitive_count(layer->canvas);
        set_prim_count(&layer->rs, prim_count);

        /* Set pixel size for render target */
        layer->rs.pixel_size.width = (float)gs.cols * cs.width;
        layer->rs.pixel_size.height = (float)gs.rows * cs.height;

        ydebug("ypaint_layer: grid=%ux%u, cell=%.1fx%.1f, prims=%u", gs.cols, gs.rows, cs.width,
               cs.height, prim_count);

        layer->base.dirty = 0;
    }

    /* Children are merged into the compiled shader by the binder. Order
     * matters here: the SDF library and every canvas font become children
     * in slot order, so the layer's font dispatcher (generated below) can
     * forward `slot` straight to the right `<ns>_…` helper. */
    size_t child_idx = 0;
    layer->rs.children[child_idx++] = &layer->sdf_lib_rs;

    uint32_t font_count = yetty_ypaint_canvas_font_count(layer->canvas);
    /* Cap at the rs.children[] capacity minus the SDF child. */
    if (font_count > YETTY_YRENDER_RS_MAX_CHILDREN - 1) {
        font_count = YETTY_YRENDER_RS_MAX_CHILDREN - 1;
    }
    /* Collect each font's resource set in slot order (slot 0 = default). */
    const struct yetty_ypaint_core_gpu_resource_set *font_rss[YETTY_YRENDER_RS_MAX_CHILDREN] = {0};
    for (uint32_t s = 0; s < font_count; s++) {
        struct yetty_ypaint_font *f = yetty_ypaint_canvas_get_font_at(layer->canvas, s);
        if (!f || !f->ops || !f->ops->get_gpu_resource_set) {
            continue;
        }
        struct yetty_yrender_gpu_resource_set_result fr = f->ops->get_gpu_resource_set(f);
        if (YETTY_IS_OK(fr)) {
            font_rss[s] = fr.value;
            layer->rs.children[child_idx++] = (struct yetty_ypaint_core_gpu_resource_set *)fr.value;
        }
    }

    /* (Re)build the per-slot dispatcher block whenever the font set grew.
     * The dispatcher forwards font_glyph_*(slot, …) to the right
     * `<ns>_…` helper from the per-instance msdf-font shaders the binder
     * has merged into the same WGSL module. */
    if (font_count != layer->last_font_count) {
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
                yetty_yrender_shader_code_set(&layer->rs.shader, layer->combined_shader,
                                              layer->combined_shader_size);
                ydebug("ypaint_layer: rebuilt dispatcher for %u fonts (%zu bytes)", font_count,
                       off);
            } else {
                free(buf);
            }
        }
    }

    /* NOTE: Complex prims (yplot, yimage, yvideo) render via their own pipelines
   * (factory pattern), not as part of ypaint layer shader dispatch. */

    layer->rs.children_count = child_idx;

    return YETTY_OK(yetty_yrender_gpu_resource_set, &layer->rs);
}

/* Keyboard input - ypaint layer doesn't handle keyboard */
static int ypaint_layer_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods)
{
    (void)self;
    (void)key;
    (void)mods;
    return 0; /* Not handled */
}

static int ypaint_layer_on_char(struct yetty_yrender_terminal_layer *self, uint32_t codepoint,
                                int mods)
{
    (void)self;
    (void)codepoint;
    (void)mods;
    return 0; /* Not handled */
}

/* YPaint layer is empty if there are no primitives */
static int ypaint_layer_is_empty(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yterm_ypaint_layer *layer = (const struct yetty_yterm_ypaint_layer *)self;

    if (!layer->canvas) {
        return 1;
    }

    return yetty_ypaint_canvas_primitive_count(layer->canvas) == 0;
}

/* Scroll - called when another layer scrolls */
static struct yetty_ycore_void_result ypaint_layer_scroll(struct yetty_yrender_terminal_layer *self,
                                                          int lines)
{
    struct yetty_yterm_ypaint_layer *layer = (struct yetty_yterm_ypaint_layer *)self;

    ydebug("ypaint_layer_scroll ENTER: lines=%d scrolling_mode=%d canvas=%p", lines,
           layer->scrolling_mode, (void *)layer->canvas);

    if (!layer->canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (!layer->scrolling_mode || lines <= 0) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result res =
        yetty_ypaint_canvas_scroll_lines(layer->canvas, (uint16_t)lines);
    if (YETTY_IS_ERR(res)) {
        return res;
    }

    layer->base.dirty = 1;

    ydebug("ypaint_layer_scroll EXIT: %d lines scrolled", lines);

    if (layer->base.request_render_fn) {
        layer->base.request_render_fn(layer->base.request_render_userdata);
    }

    return YETTY_OK_VOID();
}

/* Live anchor — canvas-line index of the live viewport top, ignoring any
 * scrollback override. The terminal uses this to convert mouse-wheel deltas
 * into a stable absolute view_top. */
static uint32_t ypaint_layer_get_live_anchor(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yterm_ypaint_layer *layer = (const struct yetty_yterm_ypaint_layer *)self;
    if (!layer->canvas) {
        return 0;
    }
    return yetty_ypaint_canvas_live_rolling_row_0((struct yetty_ypaint_canvas *)layer->canvas);
}

/* Pin / release the canvas's viewport for tmux-style scrollback view. */
static struct yetty_ycore_void_result ypaint_layer_set_view_top(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t view_top_total_idx)
{
    struct yetty_yterm_ypaint_layer *layer = (struct yetty_yterm_ypaint_layer *)self;
    if (!layer->canvas) {
        return YETTY_ERR(yetty_ycore_void, "ypaint_layer_set_view_top: NULL canvas");
    }
    struct yetty_ycore_void_result vt =
        yetty_ypaint_canvas_set_view_top(layer->canvas, active ? true : false, view_top_total_idx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vt, "ypaint_layer_set_view_top failed");
    layer->base.dirty = 1;
    if (layer->base.request_render_fn) {
        layer->base.request_render_fn(layer->base.request_render_userdata);
    }
    return YETTY_OK_VOID();
}

/* Alt-screen entry/exit: swap the active canvas with a saved one. The
 * alt canvas is built lazily on first ?1049 entry — most sessions
 * never use it, so paying the cost up-front is wasteful. Each canvas
 * keeps its own primitives, fonts, and rolling-row state; toggling is
 * just a pointer swap (no GPU work, no data copy). */
static struct yetty_ycore_void_result ypaint_layer_set_alt_screen(
    struct yetty_yrender_terminal_layer *self, int active)
{
    struct yetty_yterm_ypaint_layer *layer = (struct yetty_yterm_ypaint_layer *)self;
    int wanted = active ? 1 : 0;
    if (layer->alt_active == wanted) {
        return YETTY_OK_VOID();
    }

    /* Lazy-build the saved-side canvas the first time we toggle. The
   * empty side starts on the alt slot when we enter (alt_active=1),
   * and on the primary slot if for some reason we exit before having
   * entered (shouldn't happen, but cheap to handle). */
    if (!layer->saved_canvas && layer->create_context) {
        struct yetty_ypaint_canvas_ptr_result saved_res =
            yetty_ypaint_canvas_create(layer->scrolling_mode ? true : false, layer->create_context);
        if (YETTY_IS_ERR(saved_res)) {
            return YETTY_ERR(yetty_ycore_void, "ypaint_layer_set_alt_screen: canvas create failed",
                             saved_res);
        }
        layer->saved_canvas = saved_res.value;
        struct yetty_ycore_void_result r;
        r = yetty_ypaint_canvas_set_cell_size(layer->saved_canvas, layer->base.cell_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "set_alt_screen: set_cell_size failed");
        r = yetty_ypaint_canvas_set_grid_size(layer->saved_canvas, layer->base.grid_size);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "set_alt_screen: set_grid_size failed");
        r = yetty_ypaint_canvas_set_scroll_callback(layer->saved_canvas, on_canvas_scroll,
                                                    (struct yetty_ycore_void_result *)layer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "set_alt_screen: set_scroll_callback failed");
        r = yetty_ypaint_canvas_set_cursor_callback(layer->saved_canvas, on_canvas_cursor_set,
                                                    (struct yetty_ycore_void_result *)layer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "set_alt_screen: set_cursor_callback failed");
    }
    if (!layer->saved_canvas) {
        return YETTY_OK_VOID();
    }

    struct yetty_ypaint_canvas *tmp = layer->canvas;
    layer->canvas = layer->saved_canvas;
    layer->saved_canvas = tmp;
    layer->alt_active = wanted;

    layer->base.dirty = 1;
    if (layer->base.request_render_fn) {
        layer->base.request_render_fn(layer->base.request_render_userdata);
    }

    ydebug("ypaint: alt_screen=%d", wanted);
    return YETTY_OK_VOID();
}

/* Set cursor - called when another layer moves cursor */
static struct yetty_ycore_void_result ypaint_layer_set_cursor(
    struct yetty_yrender_terminal_layer *self, int col, int row)
{
    struct yetty_yterm_ypaint_layer *layer = (struct yetty_yterm_ypaint_layer *)self;

    if (!layer->canvas) {
        return YETTY_ERR(yetty_ycore_void, "ypaint_layer_set_cursor: NULL canvas");
    }

    struct yetty_ycore_void_result r = yetty_ypaint_canvas_set_cursor_pos(
        layer->canvas,
        (struct yetty_ycore_grid_cursor_pos){.cols = (uint32_t)col, .rows = (uint32_t)row});
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ypaint_layer_set_cursor failed");
    ydebug("ypaint_layer_set_cursor: col=%d row=%d", col, row);
    return YETTY_OK_VOID();
}

/* Render layer to target - simple prims + complex prims */
static struct yetty_ycore_void_result ypaint_layer_render(struct yetty_yrender_terminal_layer *self,
                                                          struct yetty_ypaint_core_target *target)
{
    struct yetty_yterm_ypaint_layer *layer = (struct yetty_yterm_ypaint_layer *)self;

    /* Render simple prims via render_layer */
    struct yetty_ycore_void_result res = target->ops->render_layer(target, self);
    if (!YETTY_IS_OK(res)) {
        return res;
    }

    /* Render complex prims */
    if (!layer->canvas) {
        return YETTY_OK_VOID();
    }

    uint32_t count = yetty_ypaint_canvas_complex_prim_count(layer->canvas);
    if (count == 0) {
        return YETTY_OK_VOID();
    }

    uint32_t row0 = yetty_ypaint_canvas_rolling_row_0(layer->canvas);
    struct yetty_ycore_pixel_size cell_size =
        yetty_ypaint_canvas_cell_get_pixel_size(layer->canvas);

    for (uint32_t i = 0; i < count; i++) {
        struct yetty_ypaint_core_complex_prim_instance *inst =
            yetty_ypaint_canvas_get_complex_prim(layer->canvas, i);
        if (!inst || !inst->render) {
            continue;
        }

        /* Calculate screen position from bounds and scroll offset */
        float y_offset = (float)((int32_t)inst->rolling_row - (int32_t)row0) * cell_size.height;
        float screen_x = inst->bounds.min.x;
        float screen_y = inst->bounds.min.y + y_offset;

        res = inst->render(inst, target, screen_x, screen_y);
        if (!YETTY_IS_OK(res)) {
            yerror("ypaint_layer_render: complex prim %u render failed: %s", i, res.error.msg);
            /* Continue with other prims */
        }
    }

    return YETTY_OK_VOID();
}
