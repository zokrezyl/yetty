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

struct yetty_yclass_ptr_result yetty_yvterm_shader_glyph_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yvterm_shader_glyph;
YETTY_YRESULT_DECLARE(yetty_yvterm_shader_glyph_ptr, struct yetty_yvterm_shader_glyph *);
struct yetty_yvterm_shader_glyph_ptr_result yetty_yvterm_shader_glyph_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yvterm_shader_glyph_to(struct yetty_yvterm_shader_glyph *data);

struct yetty_yclass_object_ptr_result yetty_yvterm_shader_glyph_create(
    struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yvterm_register(void);

/* Header-destined content for the generated shader-glyph-figure.h (skipped by the real build, which takes it from that header). */
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yterminal/terminal.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Shader-glyph figure — animated procedural glyphs as a compositor figure.
 *
 * Subclass of yetty_yfigure_figure. Created once per terminal, added as a
 * child of the root container under a reserved id. The container's render
 * walks the figure each frame; the figure scans the text-layer's cell
 * buffer for cells whose glyph_index has bit-31 set (PUA-B codepoints)
 * and issues a single instanced draw covering every shader-glyph cell.
 *
 * PUA range U+100000..U+100FFF (Supplementary PUA-B) is mapped 1:1 to
 * local_id 0..4095 by the text-layer's glyph resolver. Print a PUA
 * codepoint and you get an animated shader glyph at that cell.
 *
 * Why PUA-B and not BMP PUA or PUA-A: verified against the bundled
 * DejaVuSansM Nerd Font — BMP PUA has 3,488 codepoints in use, PUA-A has
 * 6,896, PUA-B has 0. Using anything other than PUA-B collides with Nerd
 * Font icons whose codepoints match a shader local-id, pinning the anim
 * timer on and burning the GPU on idle terminals.
 */

#define YETTY_SHADER_GLYPH_BASE 0x80000000u
#define YETTY_SHADER_GLYPH_PUA_BASE 0x00100000u
#define YETTY_SHADER_GLYPH_PUA_END 0x00101000u /* exclusive — 4096-slot window */

static inline int yetty_shader_glyph_is(uint32_t glyph_index)
{
    return glyph_index >= YETTY_SHADER_GLYPH_BASE;
}

static inline uint32_t yetty_shader_glyph_id_from_local(uint32_t local_id)
{
    return 0xFFFFFFFFu - local_id;
}

static inline uint32_t yetty_shader_glyph_local_id(uint32_t glyph_index)
{
    return 0xFFFFFFFFu - glyph_index;
}

static inline int yetty_shader_glyph_codepoint_in_range(uint32_t cp)
{
    return cp >= YETTY_SHADER_GLYPH_PUA_BASE && cp < YETTY_SHADER_GLYPH_PUA_END;
}

static inline uint32_t yetty_shader_glyph_id_from_codepoint(uint32_t cp)
{
    return yetty_shader_glyph_id_from_local(cp - YETTY_SHADER_GLYPH_PUA_BASE);
}

/* Inverse of _id_from_codepoint — recover the PUA codepoint that produced
 * this shader-glyph id. Used by selection extraction so a PUA cell on the
 * clipboard round-trips to itself. */
static inline uint32_t yetty_shader_glyph_codepoint_from_id(uint32_t glyph_index)
{
    return YETTY_SHADER_GLYPH_PUA_BASE + yetty_shader_glyph_local_id(glyph_index);
}

struct yetty_yvterm_shader_glyph;

/* yetty_yvterm_shader_glyph_ptr is generated (the `_from` downcast in the
 * generated header). */

/* Create the shader-glyph figure.
 *
 * `text_layer` is borrowed for cell-buffer access and must outlive the
 * figure. `rect` is the figure's absolute pixel space within its parent
 * container; for the terminal's root container this is (0,0)-(cols*cw,
 * rows*ch). `request_render_fn` is invoked from the anim timer thread
 * to nudge the event loop. Returns the yclass object handle; route it through
 * the helpers below for add_child / resize / zoom.
 *
 * Named `_figure_create` rather than `_create` so it does not collide with the
 * yclass auto-emitted `yetty_yvterm_shader_glyph_create(struct yetty_yclass_ctx
 * *)` constructor stub. */
struct yetty_yclass_object_ptr_result yetty_yvterm_shader_glyph_figure_create(
    struct yetty_ycore_rectangle rect, uint32_t cols, uint32_t rows, float cell_width,
    float cell_height, struct yetty_yrender_terminal_layer *text_layer,
    const struct yetty_context *context, yetty_yterminal_request_render_fn request_render_fn,
    void *request_render_userdata);

/* Upcast the shader-glyph object to the figure base. Stable pointer. */
struct yetty_yfigure_figure *yetty_yvterm_shader_glyph_as_figure(struct yetty_yclass_object *obj);

/* Push a new grid + cell size after a terminal resize. The figure
 * updates uniforms and its own rect to (0,0)-(cols*cw, rows*ch). Takes the
 * shader-glyph object. */
struct yetty_ycore_void_result yetty_yvterm_shader_glyph_resize(
    struct yetty_yclass_object *obj, struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size);

/* Apply visual (shader-level) zoom. Mirrors the layer op of the same
 * name — terminal broadcasts ZOOM_VISUAL_APPLY events here. Takes the
 * shader-glyph object. */
struct yetty_ycore_void_result yetty_yvterm_shader_glyph_set_visual_zoom(
    struct yetty_yclass_object *obj, float scale, float offset_x, float offset_y);

#ifdef __cplusplus
}
#endif

#endif
