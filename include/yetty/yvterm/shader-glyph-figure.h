/* GENERATED — do not edit. */
/* Public interface for regular class(es) `shader_glyph` (module: yvterm).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YVTERM_SHADER_GLYPH_FIGURE_H
#define YETTY_YCLASSGEN_YVTERM_SHADER_GLYPH_FIGURE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yterminal/terminal.h>

struct yetty_yvterm_shader_glyph;

struct yetty_yvterm_shader_glyph_ptr_result {
    int ok;
    union {
        struct yetty_yvterm_shader_glyph *value;
        struct yetty_ycore_error error;
    };
};

struct yetty_yclass_ptr_result yetty_yvterm_shader_glyph_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_yvterm_shader_glyph_ptr_result yetty_yvterm_shader_glyph_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yvterm_shader_glyph_to(struct yetty_yvterm_shader_glyph *data);

struct yetty_yclass_object_ptr_result yetty_yvterm_shader_glyph_create(
    struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yvterm_register(void);

struct yetty_yclass_object_ptr_result yetty_yvterm_shader_glyph_figure_create(
    struct yetty_ycore_rectangle rect, uint32_t cols, uint32_t rows, float cell_width,
    float cell_height, struct yetty_yrender_terminal_layer *text_layer,
    const struct yetty_context *context, yetty_yterminal_request_render_fn request_render_fn,
    void *request_render_userdata);
struct yetty_yfigure_figure *yetty_yvterm_shader_glyph_as_figure(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_shader_glyph_resize(
    struct yetty_yclass_object *obj, struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size);
struct yetty_ycore_void_result yetty_yvterm_shader_glyph_set_visual_zoom(
    struct yetty_yclass_object *obj, float scale, float offset_x, float offset_y);

#endif
