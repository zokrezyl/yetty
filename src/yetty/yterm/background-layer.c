#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <yetty/yconfig/config.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/util.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yruntime/yruntime.h>
#include <yetty/yrender/gpu-resource-set.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yterm/background-layer.h>

/* Single uniform: the fill colour. */
#define U_COLOR 0
#define U_COUNT 1

struct yetty_yterm_background_layer {
    struct yetty_yrender_terminal_layer base;

    /* Owning copy of the WGSL source — rs.shader.data points into here, so
     * it must outlive the rs. Freed in destroy. */
    struct yetty_ycore_buffer shader_code;

    struct yetty_ydraw_gpu_resource_set rs;
};

/* --- Ops -------------------------------------------------------------- */

static struct yetty_ycore_void_result bg_destroy(struct yetty_yrender_terminal_layer *self)
{
    if (!self) {
        return YETTY_OK_VOID();
    }
    struct yetty_yterm_background_layer *layer =
        container_of(self, struct yetty_yterm_background_layer, base);
    free(layer->shader_code.data);
    free(layer);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result bg_resize_grid(struct yetty_yrender_terminal_layer *self,
                                                     struct yetty_ycore_grid_size grid_size,
                                                     struct yetty_ycore_pixel_size cell_size)
{
    struct yetty_yterm_background_layer *layer =
        container_of(self, struct yetty_yterm_background_layer, base);
    self->grid_size = grid_size;
    self->cell_size = cell_size;
    layer->rs.pixel_size.width = (float)grid_size.cols * cell_size.width;
    layer->rs.pixel_size.height = (float)grid_size.rows * cell_size.height;
    self->dirty = 1;
    return YETTY_OK_VOID();
}

static struct yetty_yrender_gpu_resource_set_result bg_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yterm_background_layer *layer = container_of(
        (struct yetty_yrender_terminal_layer *)self, struct yetty_yterm_background_layer, base);
    return YETTY_OK(yetty_yrender_gpu_resource_set, &layer->rs);
}

static struct yetty_ycore_int_result bg_render(struct yetty_yrender_terminal_layer *self,
                                               struct yetty_ydraw_target *target,
                                               int force)
{
    if (!target || !target->ops || !target->ops->render_layer) {
        return YETTY_ERR(yetty_ycore_int, "background_layer: target has no render_layer");
    }
    /* bg is an opaque pane-wide fill; skipping when clean keeps the
     * cascade gate working — only redraw if dirty or forced. `force`
     * shouldn't normally be set for the background (it's at the
     * bottom of the stack so nothing below it ever renders to
     * cascade onto bg), but honour it for symmetry. */
    if (!self->dirty && !force) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    struct yetty_ycore_void_result rr = target->ops->render_layer(target, self);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, rr, "bg_render: target->render_layer");
    return YETTY_OK(yetty_ycore_int, 1);
}

/* Background is the pane wipe — never empty. Returning 0 keeps it in the
 * cascade so other panes' previous-frame pixels never bleed into this one. */
static int bg_is_empty(const struct yetty_yrender_terminal_layer *self)
{
    (void)self;
    return 0;
}

/* The bg layer's only dirty source is its own base bit (set on
 * create/resize). Stale pixels from a higher layer moving don't make bg
 * itself dirty — they make a higher layer dirty, and the terminal's
 * two-pass scan force-renders every layer (including this one) when
 * anything is dirty, which is exactly what wipes the stale region. */
static int bg_is_dirty(const struct yetty_yrender_terminal_layer *self)
{
    return self->dirty;
}

static const struct yetty_yterm_terminal_layer_ops bg_ops = {
    .destroy = bg_destroy,
    .resize_grid = bg_resize_grid,
    .get_gpu_resource_set = bg_get_gpu_resource_set,
    .render = bg_render,
    .is_dirty = bg_is_dirty,
    .is_empty = bg_is_empty,
    /* The rest stay NULL — terminal.c null-checks before invoking. */
};

/* --- Create ----------------------------------------------------------- */

struct yetty_yterm_terminal_layer_result yetty_yterm_background_layer_create(
    const struct yetty_context *yetty_context, const float color_rgba[4])
{
    if (!yetty_context) {
        return YETTY_ERR(yetty_yterm_terminal_layer, "yetty_context is NULL");
    }
    struct yetty_yconfig_config *config = yetty_context->runtime->config;
    if (!config) {
        return YETTY_ERR(yetty_yterm_terminal_layer, "config is NULL");
    }

    /* Read the WGSL source. Same pattern as the other yterm layers — the
     * file is copied into <paths/shaders>/ at build time. */
    const char *shaders_dir = config->ops->get_string(config, "paths/shaders", "");
    char shader_path[512];
    snprintf(shader_path, sizeof(shader_path), "%s/background-layer.wgsl", shaders_dir);
    struct yetty_ycore_buffer_result br = yetty_ycore_read_file(shader_path);
    if (YETTY_IS_ERR(br)) {
        return YETTY_ERR(yetty_yterm_terminal_layer, "background_layer: shader read failed", br);
    }

    struct yetty_yterm_background_layer *layer = calloc(1, sizeof(*layer));
    if (!layer) {
        free(br.value.data);
        return YETTY_ERR(yetty_yterm_terminal_layer, "alloc failed");
    }

    layer->base.ops = &bg_ops;
    layer->base.dirty = 1;
    layer->shader_code = br.value;

    /* Resource set: one VEC4 uniform, no buffers, no textures, no children. */
    strncpy(layer->rs.namespace, "background", YETTY_YRENDER_NAME_MAX - 1);
    layer->rs.namespace[YETTY_YRENDER_NAME_MAX - 1] = '\0';

    layer->rs.uniform_count = U_COUNT;
    strncpy(layer->rs.uniforms[U_COLOR].name, "color", YETTY_YRENDER_NAME_MAX - 1);
    layer->rs.uniforms[U_COLOR].name[YETTY_YRENDER_NAME_MAX - 1] = '\0';
    layer->rs.uniforms[U_COLOR].type = YETTY_YRENDER_UNIFORM_VEC4;
    if (color_rgba) {
        layer->rs.uniforms[U_COLOR].vec4[0] = color_rgba[0];
        layer->rs.uniforms[U_COLOR].vec4[1] = color_rgba[1];
        layer->rs.uniforms[U_COLOR].vec4[2] = color_rgba[2];
        layer->rs.uniforms[U_COLOR].vec4[3] = color_rgba[3];
    } else {
        layer->rs.uniforms[U_COLOR].vec4[0] = 0.0f;
        layer->rs.uniforms[U_COLOR].vec4[1] = 0.0f;
        layer->rs.uniforms[U_COLOR].vec4[2] = 0.0f;
        layer->rs.uniforms[U_COLOR].vec4[3] = 1.0f;
    }

    yetty_yrender_shader_code_set(&layer->rs.shader, (const char *)layer->shader_code.data,
                                  layer->shader_code.size);

    ydebug("background_layer: created color=(%.2f,%.2f,%.2f,%.2f)",
           layer->rs.uniforms[U_COLOR].vec4[0], layer->rs.uniforms[U_COLOR].vec4[1],
           layer->rs.uniforms[U_COLOR].vec4[2], layer->rs.uniforms[U_COLOR].vec4[3]);

    return YETTY_OK(yetty_yterm_terminal_layer, &layer->base);
}
