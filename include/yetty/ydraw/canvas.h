/* canvas.h — polymorphic ydraw canvas vtable.
 *
 * Two independent implementations fill in this vtable:
 *   - scrolling-canvas (src/yetty/ydraw/scrolling-canvas.c)
 *   - scene-canvas    (src/yetty/ydraw/scene-canvas.c)
 *
 * They share NOTHING but this header. No dispatcher functions, no shared
 * "base" struct, no shared init helpers. Consumers hold a
 * `struct yetty_ydraw_canvas *` and invoke methods directly through the
 * vtable: `canvas->ops->set_cell_size(canvas, sz)`.
 *
 * The variant struct is private to each variant's .c file; the public
 * `struct yetty_ydraw_canvas` carries only the vtable pointer, embedded
 * as the FIRST member of each variant struct, so the variant recovers
 * itself with a plain downcast (offset 0).
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <yetty/ycore/ffi-annotations.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_canvas;
struct yetty_ydraw_canvas_ops;
struct yetty_ydraw_raw_figure_factory;
struct yetty_ydraw_figure;
struct yetty_ydraw_flyweight_registry;
struct yetty_ydraw_font;
struct yetty_ywire_wire_statemachine;

/* Result type for the polymorphic pointer — every variant's create
 * returns this. */
YETTY_YRESULT_DECLARE(yetty_ydraw_canvas_ptr, struct yetty_ydraw_canvas *);

/* Scroll / cursor callback typedefs. The scrolling variant fires the
 * scroll callback whenever ingestion advances the live viewport past
 * its current bottom; cursor callback fires when the cursor moves
 * without scrolling. scene-canvas never invokes either. */
typedef struct yetty_ycore_void_result (*yetty_ydraw_canvas_scroll_callback)(void *userdata,
                                                                             uint16_t num_lines);
typedef struct yetty_ycore_void_result (*yetty_ydraw_canvas_cursor_callback)(void *userdata,
                                                                             uint16_t new_row);

/* Pointer + size view returned by build_drawable_staging.
 * word_count is 0 on an empty canvas. */
struct yetty_ydraw_drawable_staging {
    const uint32_t *data;
    uint32_t word_count;
};
YETTY_YRESULT_DECLARE(yetty_ydraw_drawable_staging, struct yetty_ydraw_drawable_staging);

/* View of a single glyph drawable. */
struct yetty_ydraw_glyph_view {
    float x;
    float y;
    uint32_t glyph_idx;
    int32_t font_slot;
};

typedef void (*yetty_ydraw_canvas_glyph_visitor)(const struct yetty_ydraw_glyph_view *view,
                                                 void *user);

/*===========================================================================
 * Vtable
 *
 * Every public operation IS the vtable. Callers invoke
 * `canvas->ops->foo(canvas, ...)` directly — there is no wrapper.
 * Both variants supply a complete vtable; ops are never NULL. (Variant-
 * specific ops with no semantic action — e.g. cursor on scene-canvas —
 * are implemented as success-returning stubs.)
 *
 * Convention: functions that DO work and can fail return a Result type;
 * plain getters that read state return the value directly.
 *===========================================================================*/

struct yetty_ydraw_canvas_ops {
    const char *name; /* "scrolling" | "scene" — diagnostics */

    struct yetty_ycore_void_result (*destroy)(
        struct yetty_ydraw_canvas *canvas YETTY_ANNOT_CALLEE_OWNED);

    /* Config */
    struct yetty_ycore_void_result (*set_cell_size)(struct yetty_ydraw_canvas *canvas,
                                                    struct yetty_ycore_pixel_size pixel_size);
    struct yetty_ycore_void_result (*set_grid_size)(struct yetty_ydraw_canvas *canvas,
                                                    struct yetty_ycore_grid_size grid_size);
    struct yetty_ycore_pixel_size (*get_cell_size)(const struct yetty_ydraw_canvas *canvas);
    struct yetty_ycore_grid_size (*get_grid_size)(const struct yetty_ydraw_canvas *canvas);

    /* OSC ingestion. */
    struct yetty_ycore_void_result (*process_input)(struct yetty_ydraw_canvas *canvas,
                                                    struct yetty_ywire_wire_statemachine *sm);

    /* Cursor / scroll. scene-canvas implements these as success-stubs. */
    struct yetty_ycore_void_result (*set_cursor_pos)(struct yetty_ydraw_canvas *canvas,
                                                     struct yetty_ycore_grid_cursor_pos pos);
    struct yetty_ycore_void_result (*scroll_lines)(struct yetty_ydraw_canvas *canvas,
                                                   uint16_t num_lines);
    struct yetty_ycore_void_result (*set_view_top)(struct yetty_ydraw_canvas *canvas, bool active,
                                                   uint32_t view_top);
    uint32_t (*rolling_row_0)(struct yetty_ydraw_canvas *canvas);
    uint32_t (*live_rolling_row_0)(struct yetty_ydraw_canvas *canvas);
    struct yetty_ycore_void_result (*set_scroll_callback)(
        struct yetty_ydraw_canvas *canvas, yetty_ydraw_canvas_scroll_callback callback,
        void *userdata);
    struct yetty_ycore_void_result (*set_cursor_callback)(
        struct yetty_ydraw_canvas *canvas, yetty_ydraw_canvas_cursor_callback callback,
        void *userdata);

    /* Packed GPU format */
    struct yetty_ycore_void_result (*mark_dirty)(struct yetty_ydraw_canvas *canvas);
    bool (*is_dirty)(const struct yetty_ydraw_canvas *canvas);

    struct yetty_ycore_void_result (*rebuild_grid)(struct yetty_ydraw_canvas *canvas);
    const uint32_t *(*grid_data)(const struct yetty_ydraw_canvas *canvas);
    uint32_t (*grid_word_count)(const struct yetty_ydraw_canvas *canvas);

    struct yetty_ydraw_drawable_staging_result (*build_drawable_staging)(
        struct yetty_ydraw_canvas *canvas);
    uint32_t (*drawable_gpu_size)(const struct yetty_ydraw_canvas *canvas);

    /* State */
    struct yetty_ycore_void_result (*clear)(struct yetty_ydraw_canvas *canvas);
    uint32_t (*drawable_count)(const struct yetty_ydraw_canvas *canvas);

    /* Fonts */
    uint32_t (*font_count)(const struct yetty_ydraw_canvas *canvas);
    uint32_t (*font_generation)(const struct yetty_ydraw_canvas *canvas);
    struct yetty_ydraw_font *(*get_font_at)(const struct yetty_ydraw_canvas *canvas, uint32_t slot);
    struct yetty_ydraw_font *(*get_default_font)(const struct yetty_ydraw_canvas *canvas);

    /* Flyweight registry / complex-prim factory accessors. */
    const struct yetty_ydraw_flyweight_registry *(*get_flyweight_registry)(
        const struct yetty_ydraw_canvas *canvas);
    struct yetty_ydraw_raw_figure_factory *(*get_figure_factory)(
        const struct yetty_ydraw_canvas *canvas);

    /* Complex drawable access (for atlas rendering). */
    uint32_t (*figure_count)(const struct yetty_ydraw_canvas *canvas);
    struct yetty_ydraw_figure *(*get_figure)(const struct yetty_ydraw_canvas *canvas,
                                             uint32_t index);

    /* Glyph iteration. Does work + invokes the visitor — must surface
     * any internal failure (alloc, decode of evicted lines, ...) via
     * the Result. */
    struct yetty_ycore_void_result (*for_each_glyph)(struct yetty_ydraw_canvas *canvas,
                                                     yetty_ydraw_canvas_glyph_visitor visitor,
                                                     void *user);
};

/*===========================================================================
 * Polymorphic handle — JUST a vtable pointer.
 *
 * Every variant struct embeds `struct yetty_ydraw_canvas base` as its
 * first member. The variant fills `base.ops` at create time; recovery
 * inside vtable methods is a plain downcast since `base` is at offset 0.
 *===========================================================================*/

struct yetty_ydraw_canvas {
    const struct yetty_ydraw_canvas_ops *ops;
};

#ifdef __cplusplus
}
#endif
