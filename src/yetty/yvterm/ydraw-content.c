#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/util.h>
#include <yetty/yfont/font.h>
#include <yetty/yface/yface.h>
#include <yetty/yplatform/ycoroutine.h>
#include <yetty/ywire/wire-statemachine.h>
#include <yetty/ydraw-core/composite.h>
#include <yetty/ydraw-factory/composite-factory.h>
#include <yetty/ydraw/canvas.h>
#include <yetty/ydraw/scrolling-canvas.h>
#include <yetty/yrender/font-dispatcher.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yvterm/osc-args.h>
#include <yetty/yterminal/dcs-codes.h> /* YETTY_DCS_YDRAW_* */
#include <yetty/yvterm/ydraw-content.h>
#include <yetty/yconfig/config.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yframework/yframework.h>
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
static inline void set_grid_size(struct yetty_yrender_gpu_resource_set *rs, float cols, float rows)
{
    rs->uniforms[U_GRID_SIZE].vec2[0] = cols;
    rs->uniforms[U_GRID_SIZE].vec2[1] = rows;
}
static inline void set_cell_size(struct yetty_yrender_gpu_resource_set *rs, float w, float h)
{
    rs->uniforms[U_CELL_SIZE].vec2[0] = w;
    rs->uniforms[U_CELL_SIZE].vec2[1] = h;
}
static inline void set_rolling_row_0(struct yetty_yrender_gpu_resource_set *rs, uint32_t row_origin)
{
    rs->uniforms[U_ROLLING_ROW_0].u32 = row_origin;
}
static inline void set_drawable_count(struct yetty_yrender_gpu_resource_set *rs, uint32_t count)
{
    rs->uniforms[U_PRIM_COUNT].u32 = count;
}
static inline void set_visual_zoom(struct yetty_yrender_gpu_resource_set *rs, float scale,
                                   float off_x, float off_y)
{
    rs->uniforms[U_VZ_SCALE].f32 = scale;
    rs->uniforms[U_VZ_OFF].vec2[0] = off_x;
    rs->uniforms[U_VZ_OFF].vec2[1] = off_y;
}
static inline void set_cell_zoom(struct yetty_yrender_gpu_resource_set *rs, float scale,
                                 float off_x, float off_y)
{
    rs->uniforms[U_CZ_SCALE].f32 = scale;
    rs->uniforms[U_CZ_OFF].vec2[0] = off_x;
    rs->uniforms[U_CZ_OFF].vec2[1] = off_y;
}

/* Init uniforms */
static void init_uniforms(struct yetty_yrender_gpu_resource_set *rs)
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
struct yetty_yvterm_ydraw_content {
    struct yetty_yrender_terminal_layer base;
    /* Initial cell size captured at creation — used to derive the cumulative
   * cell-zoom factor and push it to composite factories (yplot, …) so
   * their shaders can apply the "intrusive" zoom the same way they apply
   * the non-intrusive visual zoom. */
    struct yetty_ycore_pixel_size initial_cell_size;

    /* Child resource set that holds the generated SDF library (ysdf.gen.wgsl:
   * sdf_* functions + evaluate_sdf_2d dispatcher). Merged into the final
   * shader by the binder via rs.children[], so the layer never has to
   * handwrite SDF cases — regenerate the .wgsl via gen-sdf-code.py instead. */
    struct yetty_ycore_buffer sdf_lib_code;
    struct yetty_yrender_gpu_resource_set sdf_lib_rs;
    struct yetty_ydraw_canvas *canvas;
    struct yetty_yrender_gpu_resource_set rs;
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
    enum yetty_yvterm_ydraw_content_kind kind;

    /* Cached at create — needed to lazily build the alt-screen canvas
   * on first ?1049 toggle. */
    const struct yetty_context *create_context;

    /* Alt-screen state. The active canvas is `canvas` above; the saved
   * counterpart (primary while in alt, alt while in primary) lives
   * here. Toggle via ydraw_content_set_alt_screen swaps the two. */
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
static struct yetty_ycore_void_result ydraw_content_destroy(
    struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result ydraw_content_resize_grid(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size);
static struct yetty_yrender_gpu_resource_set_result ydraw_content_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self);
static int ydraw_content_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods);
static int ydraw_content_on_char(struct yetty_yrender_terminal_layer *self, uint32_t codepoint,
                                 int mods);
static int ydraw_content_is_empty(const struct yetty_yrender_terminal_layer *self);
static int ydraw_content_is_dirty(const struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result ydraw_content_scroll(
    struct yetty_yrender_terminal_layer *self, int lines);
static struct yetty_ycore_void_result ydraw_content_set_cursor(
    struct yetty_yrender_terminal_layer *self, int col, int row);
static struct yetty_ycore_int_result ydraw_content_render(struct yetty_yrender_terminal_layer *self,
                                                          struct yetty_ydraw_target *target,
                                                          int force);
static uint32_t ydraw_content_get_live_anchor(const struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result ydraw_content_set_view_top(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t view_top_total_idx);
static struct yetty_ycore_void_result ydraw_content_set_alt_screen(
    struct yetty_yrender_terminal_layer *self, int active);
static struct yetty_ycore_void_result ydraw_content_clear_screen(
    struct yetty_yrender_terminal_layer *self);
static struct yetty_ycore_void_result ydraw_content_set_selection(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t anchor_row, uint32_t anchor_col,
    uint32_t head_row, uint32_t head_col);
static struct yetty_ycore_void_result ydraw_content_get_selection_text(
    const struct yetty_yrender_terminal_layer *self, struct yetty_ycore_buffer *out);

/* Canvas scroll callback - propagate to other layers */
static struct yetty_ycore_void_result on_canvas_scroll(void *user_data, uint16_t num_lines)
{
    struct yetty_yvterm_ydraw_content *layer = user_data;
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
    struct yetty_yvterm_ydraw_content *layer = user_data;
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

static struct yetty_ycore_void_result ydraw_content_set_visual_zoom(
    struct yetty_yrender_terminal_layer *self, float scale, float off_x, float off_y)
{
    struct yetty_yvterm_ydraw_content *layer = (struct yetty_yvterm_ydraw_content *)self;
    set_visual_zoom(&layer->rs, scale, off_x, off_y);
    self->dirty = 1;

    /* Complex drawables (yplot / yimage / …) render through their own
     * pipelines with their own fragment shaders — they don't go through the
     * ydraw-layer shader. Push the zoom into every concrete factory's shared
     * uniforms so each type's shader can apply the same transform. */
    if (layer->canvas) {
        struct yetty_ydraw_composite_factory *f =
            layer->canvas->ops->get_composite_factory(layer->canvas);
        yetty_ydraw_composite_factory_set_visual_zoom(f, scale, off_x, off_y);
    }
    return YETTY_OK_VOID();
}

/* Ops */
static const struct yetty_yterminal_layer_ops ydraw_content_ops = {
    .destroy = ydraw_content_destroy,
    .resize_grid = ydraw_content_resize_grid,
    .set_visual_zoom = ydraw_content_set_visual_zoom,
    .get_gpu_resource_set = ydraw_content_get_gpu_resource_set,
    .render = ydraw_content_render,
    .is_dirty = ydraw_content_is_dirty,
    .is_empty = ydraw_content_is_empty,
    .on_key = ydraw_content_on_key,
    .on_char = ydraw_content_on_char,
    .scroll = ydraw_content_scroll,
    .set_cursor = ydraw_content_set_cursor,
    .get_live_anchor = ydraw_content_get_live_anchor,
    .set_view_top = ydraw_content_set_view_top,
    .set_alt_screen = ydraw_content_set_alt_screen,
    .clear_screen = ydraw_content_clear_screen,
    .set_selection = ydraw_content_set_selection,
    .get_selection_text = ydraw_content_get_selection_text,
};

/* Create */
struct yetty_yterminal_layer_result yetty_yvterm_ydraw_content_create(
    enum yetty_yvterm_ydraw_content_kind kind, uint32_t cols, uint32_t rows, float cell_width,
    float cell_height, const struct yetty_context *context,
    yetty_yterminal_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterminal_scroll_fn scroll_fn, void *scroll_userdata, yetty_yterminal_cursor_fn cursor_fn,
    void *cursor_userdata)
{
    struct yetty_yvterm_ydraw_content *layer;

    /* Load ydraw-layer shader from file */
    struct yetty_yconfig_config *config = context->runtime->config;
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
        return YETTY_ERR(yetty_yterminal_layer,
                         "ydraw_content_create: read_file(ydraw-layer.wgsl) failed", shader_res);
    }
    struct yetty_ycore_buffer_result sdf_lib_res = yetty_ycore_read_file(sdf_lib_path);
    if (YETTY_IS_ERR(sdf_lib_res)) {
        free(shader_res.value.data);
        return YETTY_ERR(yetty_yterminal_layer, "ydraw_content_create: read_file(sdf_lib) failed",
                         sdf_lib_res);
    }

    layer = calloc(1, sizeof(struct yetty_yvterm_ydraw_content));
    if (!layer) {
        free(shader_res.value.data);
        free(sdf_lib_res.value.data);
        return YETTY_ERR(yetty_yterminal_layer, "failed to allocate ydraw layer");
    }
    layer->shader_code = shader_res.value;
    layer->sdf_lib_code = sdf_lib_res.value;
    strncpy(layer->sdf_lib_rs.namespace, "ysdf_lib", YETTY_YRENDER_NAME_MAX - 1);
    yetty_yrender_shader_code_set(&layer->sdf_lib_rs.shader, (const char *)layer->sdf_lib_code.data,
                                  layer->sdf_lib_code.size);

    layer->base.ops = &ydraw_content_ops;
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

    /* Create the canvas. Only KIND_SCROLLING remains — scene-canvas
     * was retired with the ycompositor migration. The kind parameter
     * is kept on the create signature for source compatibility with
     * existing call sites that pass SCROLLING explicitly. */
    if (!context) {
        free(layer);
        return YETTY_ERR(yetty_yterminal_layer, "context is NULL");
    }
    if (kind != YETTY_YVTERM_YDRAW_CONTENT_KIND_SCROLLING) {
        free(layer);
        return YETTY_ERR(yetty_yterminal_layer, "ydraw-layer: only KIND_SCROLLING is supported");
    }
    struct yetty_ydraw_canvas_ptr_result canvas_res = yetty_ydraw_scrolling_canvas_create(context);
    if (YETTY_IS_ERR(canvas_res)) {
        free(layer);
        return YETTY_ERR(yetty_yterminal_layer, "ydraw-layer: canvas create failed", canvas_res);
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
            return YETTY_ERR(yetty_yterminal_layer,
                             "ydraw_content_create: yetty_yface_create failed", yr);
        }
        layer->yface = yr.value;
    }

    ydebug("ydraw_content_create: kind=%s, %ux%u grid, %.1fx%.1f cells",
           kind == YETTY_YVTERM_YDRAW_CONTENT_KIND_SCROLLING ? "scrolling" : "scene", cols, rows,
           cell_width, cell_height);

    return YETTY_OK(yetty_yterminal_layer, &layer->base);
}

/* Destroy */
static struct yetty_ycore_void_result ydraw_content_destroy(
    struct yetty_yrender_terminal_layer *self)
{
    struct yetty_yvterm_ydraw_content *layer = (struct yetty_yvterm_ydraw_content *)self;
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
        return YETTY_ERR(yetty_ycore_void, "ydraw_content_destroy: canvas destroy failed",
                         first_err);
    }
    return YETTY_OK_VOID();
}

/* Process — persistent layer coro entry. The wire-statemachine resumes
 * this once and treats a return as fatal layer exit (with the side
 * effect that any partial envelope body still in the SM ring gets
 * leaked to the default text-layer as raw text on respawn). So we must
 * loop forever, yielding after each envelope.
 *
 * Each iteration handles one envelope:
 *   CLEAR              — wipe the canvas, no body to drain.
 *   BIN / OVERLAY      — framed YDrawList; canvas->process_input streams
 *   SCENE_BIN            it off the SM (b64 + lz4 decoded by the SM).
 *                        Those canvas implementations own their own
 *                        long-lived loop, so the call doesn't return —
 *                        the outer for(;;) is reached only on the CLEAR
 *                        path. That's fine: only the layers registered
 *                        for CLEAR need the loop to survive that code.
 * YAML is no longer accepted on the wire — yaml is producer-side only.
 *
 * userdata is the layer's base pointer. */
struct yetty_ycore_void_result yetty_yvterm_ydraw_content_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *osc_statemachine)
{
    struct yetty_yrender_terminal_layer *self = userdata;
    struct yetty_yvterm_ydraw_content *layer = (struct yetty_yvterm_ydraw_content *)self;

    for (;;) {
        int code = yetty_ywire_wire_statemachine_code(osc_statemachine);
        struct yetty_ycore_void_result r;
        switch (code) {
        case YETTY_DCS_YDRAW_CLEAR:
            r = layer->canvas->ops->clear(layer->canvas);
            /* CLEAR carries no payload, but its envelope terminator still
             * has to be drained off the state machine — otherwise
             * terminator_seen never gets set, the SM stalls inside this
             * envelope, and the next envelope (typically the YDRAW_BIN that
             * follows a clear+bin re-render) is silently dropped. The body
             * is empty, so a single read consumes the terminator; loop to
             * stay robust against a fragmented read landing on the boundary. */
            while (YETTY_IS_OK(r) && !yetty_ywire_wire_statemachine_at_end(osc_statemachine)) {
                uint8_t drain[16];
                struct yetty_ycore_size_result drain_result =
                    yetty_ywire_wire_statemachine_read(osc_statemachine, drain, sizeof(drain));
                if (YETTY_IS_ERR(drain_result)) {
                    r = YETTY_ERR(yetty_ycore_void, "ydraw: clear drain", drain_result);
                    break;
                }
                if (drain_result.value == 0 &&
                    !yetty_ywire_wire_statemachine_at_end(osc_statemachine)) {
                    yetty_yplatform_coro_yield();
                }
            }
            break;
        case YETTY_DCS_YDRAW_BIN:
        case YETTY_DCS_YDRAW_OVERLAY:
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
        /* Yield so the SM can clear terminator_seen and queue the next
         * envelope's body before resuming us. */
        yetty_yplatform_coro_yield();
    }
}

struct yetty_ycore_void_result yetty_yvterm_ydraw_content_set_cell_source(
    struct yetty_yrender_terminal_layer *self, const struct yetty_ydraw_cell_source *source)
{
    struct yetty_yvterm_ydraw_content *layer = (struct yetty_yvterm_ydraw_content *)self;

    /* Apply to the active canvas and the saved (alt-screen) canvas if one
     * exists — the same cell source serves both, since handle_at always hits
     * libvterm's currently-active buffer. A canvas minted later on alt-screen
     * enable picks the source up when it is bound. */
    if (layer->canvas && layer->canvas->ops->set_cell_source) {
        struct yetty_ycore_void_result r =
            layer->canvas->ops->set_cell_source(layer->canvas, source);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ydraw_content_set_cell_source: canvas");
    }
    if (layer->saved_canvas && layer->saved_canvas->ops->set_cell_source) {
        struct yetty_ycore_void_result r =
            layer->saved_canvas->ops->set_cell_source(layer->saved_canvas, source);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ydraw_content_set_cell_source: saved canvas");
    }
    return YETTY_OK_VOID();
}

/* Single atomic update: both grid_size and cell_size. The canvas's
 * grid_pixel area is `cols * cell_w x rows * cell_h`, which the shader
 * uses 1:1 to map primitive coords to fragments. Callers compute
 * cell_size = client_area / rows so the canvas matches the framebuffer
 * exactly (no remainder pixels, no NDC-stretch). Replaces the old
 * resize_grid (grid only) + set_cell_size (zoom uniform only) pair. */
static struct yetty_ycore_void_result ydraw_content_resize_grid(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size)
{
    struct yetty_yvterm_ydraw_content *layer = (struct yetty_yvterm_ydraw_content *)self;

    if (!layer->canvas) {
        return YETTY_ERR(yetty_ycore_void, "canvas is NULL");
    }
    if (cell_size.width <= 0.0f || cell_size.height <= 0.0f) {
        return YETTY_ERR(yetty_ycore_void, "ydraw_content_resize_grid: invalid cell size");
    }

    struct yetty_ycore_void_result cr = layer->canvas->ops->set_cell_size(layer->canvas, cell_size);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "ydraw_content_resize_grid: canvas set_cell_size");

    self->cell_size = cell_size;
    self->grid_size = grid_size;
    layer->canvas->ops->set_grid_size(layer->canvas, grid_size);

    /* The old set_cell_size also drove a cell_zoom shader uniform for
     * the structural-zoom path (scaling primitives in-place without
     * touching the canvas). Now the canvas's cell stride moves directly,
     * so the uniform stays at the identity factor. */
    float base_h = layer->initial_cell_size.height;
    float cz = (base_h > 0.0f) ? (cell_size.height / base_h) : 1.0f;
    set_cell_zoom(&layer->rs, cz, 0.0f, 0.0f);
    struct yetty_ydraw_composite_factory *ff =
        layer->canvas->ops->get_composite_factory(layer->canvas);
    yetty_ydraw_composite_factory_set_cell_zoom(ff, cz, 0.0f, 0.0f);

    self->dirty = 1;

    ydebug("ydraw_content_resize_grid: grid=%ux%u cell=%.2fx%.2f", grid_size.cols, grid_size.rows,
           cell_size.width, cell_size.height);
    return YETTY_OK_VOID();
}

/* Get GPU resource set */
static struct yetty_yrender_gpu_resource_set_result ydraw_content_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self)
{
    struct yetty_yvterm_ydraw_content *layer = (struct yetty_yvterm_ydraw_content *)self;

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
                             "ydraw_content_get_gpu_resource_set: build_drawable_staging failed",
                             ps_r);
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

        ydebug("ydraw_content: grid=%ux%u, cell=%.1fx%.1f, prims=%u", gs.cols, gs.rows, cs.width,
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
    const struct yetty_yrender_gpu_resource_set *font_rss[YETTY_YRENDER_RS_MAX_CHILDREN] = {0};
    for (uint32_t s = 0; s < font_count; s++) {
        struct yetty_yfont_font *f = layer->canvas->ops->get_font_at(layer->canvas, s);
        if (!f || !f->ops || !f->ops->get_gpu_resource_set) {
            continue;
        }
        struct yetty_yrender_gpu_resource_set_result fr = f->ops->get_gpu_resource_set(f);
        if (YETTY_IS_OK(fr)) {
            font_rss[s] = fr.value;
            layer->rs.children[child_idx++] = (struct yetty_yrender_gpu_resource_set *)fr.value;
        }
    }

    /* (Re)build the per-slot dispatcher block whenever the active font
     * set changes — both growth (new slot) and shrinkage (slot dropped
     * by eviction). The cache's generation counter bumps on alloc AND
     * release, unlike font_count which is a high watermark. */
    uint32_t font_generation = layer->canvas->ops->font_generation(layer->canvas);
    if (font_generation != layer->last_font_generation) {
        const char *slot_namespaces[YETTY_YRENDER_RS_MAX_CHILDREN];
        for (uint32_t slot = 0; slot < font_count; slot++) {
            slot_namespaces[slot] = font_rss[slot] ? font_rss[slot]->namespace : NULL;
        }

        char *dispatcher_wgsl = NULL;
        size_t dispatcher_size = 0;
        struct yetty_ycore_void_result dispatcher_result = yetty_yrender_build_font_dispatcher_wgsl(
            slot_namespaces, font_count, &dispatcher_wgsl, &dispatcher_size);
        if (YETTY_IS_OK(dispatcher_result)) {
            size_t combined_size = dispatcher_size + layer->shader_code.size;
            char *combined_buffer = malloc(combined_size + 1u);
            if (combined_buffer) {
                memcpy(combined_buffer, dispatcher_wgsl, dispatcher_size);
                memcpy(combined_buffer + dispatcher_size, layer->shader_code.data,
                       layer->shader_code.size);
                combined_buffer[combined_size] = '\0';

                free(layer->combined_shader);
                layer->combined_shader = combined_buffer;
                layer->combined_shader_size = combined_size;
                layer->last_font_count = font_count;
                layer->last_font_generation = font_generation;
                yetty_yrender_shader_code_set(&layer->rs.shader, layer->combined_shader,
                                              layer->combined_shader_size);
                ydebug("ydraw_content: rebuilt dispatcher for %u fonts gen=%u (%zu bytes)",
                       font_count, font_generation, combined_size);
            }
            free(dispatcher_wgsl);
        } else {
            yetty_ycore_error_destroy(dispatcher_result.error);
        }
    }

    /* NOTE: Complex prims (yplot, yimage, yvideo) render via their own pipelines
   * (factory pattern), not as part of ydraw layer shader dispatch. */

    layer->rs.children_count = child_idx;

    return YETTY_OK(yetty_yrender_gpu_resource_set, &layer->rs);
}

/* Keyboard input - ydraw layer doesn't handle keyboard */
static int ydraw_content_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods)
{
    (void)self;
    (void)key;
    (void)mods;
    return 0; /* Not handled */
}

static int ydraw_content_on_char(struct yetty_yrender_terminal_layer *self, uint32_t codepoint,
                                 int mods)
{
    (void)self;
    (void)codepoint;
    (void)mods;
    return 0; /* Not handled */
}

/* YDraw layer is empty if there are no drawables */
static int ydraw_content_is_empty(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yvterm_ydraw_content *layer =
        (const struct yetty_yvterm_ydraw_content *)self;

    if (!layer->canvas) {
        return 1;
    }

    return layer->canvas->ops->drawable_count(layer->canvas) == 0;
}

/* Mirrors every dirty source ydraw_content_render reads: the base bit, the
 * canvas's content dirty bit (simple prims), and any figure_instance with
 * its own dirty bit set. Must stay in sync with the two-step render — if a
 * new dirty source is added there, add it here too. */
static int ydraw_content_is_dirty(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yvterm_ydraw_content *layer =
        (const struct yetty_yvterm_ydraw_content *)self;

    if (self->dirty) {
        return 1;
    }
    if (!layer->canvas || !layer->canvas->ops) {
        return 0;
    }
    if (layer->canvas->ops->is_dirty(layer->canvas)) {
        return 1;
    }
    uint32_t count = layer->canvas->ops->figure_count(layer->canvas);
    for (uint32_t i = 0; i < count; i++) {
        struct yetty_ydraw_composite *inst = layer->canvas->ops->get_figure(layer->canvas, i);
        if (inst && inst->dirty) {
            return 1;
        }
    }
    return 0;
}

/* Scroll - called when another layer scrolls */
static struct yetty_ycore_void_result ydraw_content_scroll(
    struct yetty_yrender_terminal_layer *self, int lines)
{
    struct yetty_yvterm_ydraw_content *layer = (struct yetty_yvterm_ydraw_content *)self;

    ydebug("ydraw_content_scroll ENTER: lines=%d canvas=%p", lines, (void *)layer->canvas);

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

    ydebug("ydraw_content_scroll EXIT: %d lines scrolled", lines);

    if (layer->base.request_render_fn) {
        layer->base.request_render_fn(layer->base.request_render_userdata);
    }

    return YETTY_OK_VOID();
}

/* Live anchor — canvas-line index of the live viewport top, ignoring any
 * scrollback override. The terminal uses this to convert mouse-wheel deltas
 * into a stable absolute view_top. */
static uint32_t ydraw_content_get_live_anchor(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yvterm_ydraw_content *layer =
        (const struct yetty_yvterm_ydraw_content *)self;
    if (!layer->canvas) {
        return 0;
    }
    return layer->canvas->ops->live_rolling_row_0(layer->canvas);
}

/* Pin / release the canvas's viewport for tmux-style scrollback view. */
static struct yetty_ycore_void_result ydraw_content_set_view_top(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t view_top_total_idx)
{
    struct yetty_yvterm_ydraw_content *layer = (struct yetty_yvterm_ydraw_content *)self;
    if (!layer->canvas) {
        return YETTY_ERR(yetty_ycore_void, "ydraw_content_set_view_top: NULL canvas");
    }
    struct yetty_ycore_void_result vt =
        layer->canvas->ops->set_view_top(layer->canvas, active ? true : false, view_top_total_idx);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, vt, "ydraw_content_set_view_top failed");
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
static struct yetty_ycore_void_result ydraw_content_set_alt_screen(
    struct yetty_yrender_terminal_layer *self, int active)
{
    struct yetty_yvterm_ydraw_content *layer = (struct yetty_yvterm_ydraw_content *)self;
    int wanted = active ? 1 : 0;
    if (layer->alt_active == wanted) {
        return YETTY_OK_VOID();
    }

    /* Lazy-build the saved-side canvas the first time we toggle. Only
   * KIND_SCROLLING exists now — scene-canvas was retired with the
   * ycompositor migration. */
    if (!layer->saved_canvas && layer->create_context) {
        struct yetty_ydraw_canvas_ptr_result saved_res =
            yetty_ydraw_scrolling_canvas_create(layer->create_context);
        if (YETTY_IS_ERR(saved_res)) {
            return YETTY_ERR(yetty_ycore_void, "ydraw_content_set_alt_screen: canvas create failed",
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

/* Full-screen erase (CSI 2J / 3J). With scene-canvas retired, the only
 * remaining canvas kind is SCROLLING — and that one is for the ydraw
 * OSC stream (ycat images / plots), which is NOT in scope for the
 * libvterm grid erase. Clearing it disturbs the layer's cursor /
 * rolling state and breaks the post-feed dirty handshake. So this
 * entry point is now a no-op; it stays on the vtable so the terminal's
 * dispatch doesn't need an existence check. */
static struct yetty_ycore_void_result ydraw_content_clear_screen(
    struct yetty_yrender_terminal_layer *self)
{
    (void)self;
    return YETTY_OK_VOID();
}

/* Set cursor - called when another layer moves cursor */
static struct yetty_ycore_void_result ydraw_content_set_cursor(
    struct yetty_yrender_terminal_layer *self, int col, int row)
{
    struct yetty_yvterm_ydraw_content *layer = (struct yetty_yvterm_ydraw_content *)self;

    if (!layer->canvas) {
        return YETTY_ERR(yetty_ycore_void, "ydraw_content_set_cursor: NULL canvas");
    }

    struct yetty_ycore_void_result r = layer->canvas->ops->set_cursor_pos(
        layer->canvas,
        (struct yetty_ycore_grid_cursor_pos){.cols = (uint32_t)col, .rows = (uint32_t)row});
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ydraw_content_set_cursor failed");
    ydebug("ydraw_content_set_cursor: col=%d row=%d", col, row);
    return YETTY_OK_VOID();
}

/* Render the ydraw layer to target.
 *
 * Two independent dirty sources inside this layer:
 *   1. simple prims (SDF / fonts / text-span) — driven by `self->dirty`
 *      and the canvas's grid mutations (process_input bumps it).
 *   2. each figure_instance — driven by `inst->dirty` (set by the
 *      figure's own event listener or by the canvas when a fresh
 *      envelope creates / updates it).
 *
 * `force` (from terminal_render_frame's cascade) forces everything
 * regardless of dirty bits. Returns 1 iff something was actually
 * drawn; 0 if the layer was clean and not forced and skipped
 * entirely. Bails immediately on any inner error — never silently
 * continues. */
static struct yetty_ycore_int_result ydraw_content_render(struct yetty_yrender_terminal_layer *self,
                                                          struct yetty_ydraw_target *target,
                                                          int force)
{
    struct yetty_yvterm_ydraw_content *layer = (struct yetty_yvterm_ydraw_content *)self;
    if (!layer->canvas || !layer->canvas->ops) {
        return YETTY_ERR(yetty_ycore_int, "ydraw_content_render: canvas / ops NULL");
    }

    int drew = 0;

    /* Step 1: simple prims. Iff the layer's own dirty bit or the
     * canvas's content dirty bit is set, or we're being forced. */
    int simple_dirty = self->dirty || layer->canvas->ops->is_dirty(layer->canvas);
    if (simple_dirty || force) {
        struct yetty_ycore_void_result rr = target->ops->render_layer(target, self);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rr,
                            "ydraw_content_render: render_layer (simple prims)");
        /* get_gpu_resource_set inside render_layer already cleared
         * self->dirty when it rebuilt the staging — we just record
         * that we drew. */
        drew = 1;
    }

    /* Step 2: figures. Each one decides on its own dirty bit, but
     * `force` overrides — a lower-layer redraw clobbered our pixels
     * in the shared big_target, so every figure must repaint. */
    uint32_t count = layer->canvas->ops->figure_count(layer->canvas);
    if (count == 0) {
        return YETTY_OK(yetty_ycore_int, drew);
    }
    uint32_t row0 = layer->canvas->ops->rolling_row_0(layer->canvas);
    struct yetty_ycore_pixel_size cell_size = layer->canvas->ops->get_cell_size(layer->canvas);

    for (uint32_t i = 0; i < count; i++) {
        struct yetty_ydraw_composite *inst = layer->canvas->ops->get_figure(layer->canvas, i);
        if (!inst) {
            return YETTY_ERR(yetty_ycore_int,
                             "ydraw_content_render: get_figure returned NULL within figure_count");
        }
        if (!inst->render) {
            return YETTY_ERR(yetty_ycore_int,
                             "ydraw_content_render: figure_instance has no render op");
        }
        /* Intentional skip — not an error, just nothing to redraw. */
        if (!inst->dirty && !force) {
            continue;
        }
        float y_offset = (float)((int32_t)inst->rolling_row - (int32_t)row0) * cell_size.height;
        float screen_x = inst->bounds.min.x;
        float screen_y = inst->bounds.min.y + y_offset;

        struct yetty_ycore_void_result rr = inst->render(inst, target, screen_x, screen_y);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, rr, "ydraw_content_render: figure inst->render");
        inst->dirty = 0;
        drew = 1;
    }
    return YETTY_OK(yetty_ycore_int, drew);
}

/* Selection on the ydraw layer is row-only — the column part of the
 * terminal-wide (anchor, head) pair has no meaning on rich content. We
 * collapse to [min_row, max_row] and stash it for the text extractor.
 * Highlight rendering is intentionally not wired up yet; the text-layer
 * already shows the user where their selection band is. */
static struct yetty_ycore_void_result ydraw_content_set_selection(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t anchor_row, uint32_t anchor_col,
    uint32_t head_row, uint32_t head_col)
{
    struct yetty_yvterm_ydraw_content *layer =
        container_of(self, struct yetty_yvterm_ydraw_content, base);
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
 * The ydraw canvas stores text as drawable-list entry GLYPH drawables — each one
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

static void ydraw_content_collect_visitor(const struct yetty_ydraw_glyph_view *gv, void *user)
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

static int ydraw_content_glyph_view_compare(const void *a, const void *b)
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
static size_t ydraw_content_cp_to_utf8(uint32_t cp, uint8_t out[4])
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

static struct yetty_ycore_void_result ydraw_content_get_selection_text(
    const struct yetty_yrender_terminal_layer *self, struct yetty_ycore_buffer *out)
{
    struct yetty_yvterm_ydraw_content *layer = container_of(
        (struct yetty_yrender_terminal_layer *)self, struct yetty_yvterm_ydraw_content, base);

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
            layer->canvas->ops->for_each_glyph(layer->canvas, ydraw_content_collect_visitor, &ctx);
        if (YETTY_IS_ERR(gr)) {
            free(ctx.arr);
            return YETTY_ERR(yetty_ycore_void, "ydraw_content: for_each_glyph failed", gr);
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

    qsort(ctx.arr, ctx.count, sizeof(ctx.arr[0]), ydraw_content_glyph_view_compare);

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
        struct yetty_yfont_font *font = layer->canvas->ops->get_font_at(layer->canvas, slot);
        uint32_t cp = 0xFFFD; /* fall back to U+FFFD on any failure */
        if (font && font->ops && font->ops->get_codepoint) {
            struct uint32_result cr = font->ops->get_codepoint(font, ctx.arr[i].glyph_idx);
            if (YETTY_IS_OK(cr)) {
                cp = cr.value;
            }
        }
        uint8_t utf8[4];
        size_t n = ydraw_content_cp_to_utf8(cp, utf8);
        rret = yetty_ycore_buffer_write(out, utf8, n);
        if (!YETTY_IS_OK(rret)) {
            goto done;
        }
    }

done:
    free(ctx.arr);
    return rret;
}
