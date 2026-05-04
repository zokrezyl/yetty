// YPaint Canvas - Spatial grid for GPU primitive lookup
// Rolling offset approach: O(1) scroll, no per-primitive updates

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ypaint_canvas;
struct yetty_context;

YETTY_YRESULT_DECLARE(yetty_ypaint_canvas_ptr, struct yetty_ypaint_canvas *);

// Create a canvas
// @param scrolling_mode If true, primitives are cursor-relative and scroll
// @param context Yetty context for config access (fonts path, etc.)
struct yetty_ypaint_canvas_ptr_result yetty_ypaint_canvas_create(
    bool scrolling_mode, const struct yetty_context *context);

// Destroy a canvas
struct yetty_ycore_void_result yetty_ypaint_canvas_destroy(struct yetty_ypaint_canvas *canvas);

//=============================================================================
// Configuration
//=============================================================================

// Set grid cell size (pixels)
struct yetty_ycore_void_result yetty_ypaint_canvas_set_cell_size(struct yetty_ypaint_canvas *canvas,
                                                                 struct yetty_ycore_pixel_size pixel_size);
// Set grid dimensions (cols/rows)
struct yetty_ycore_void_result yetty_ypaint_canvas_set_grid_size(struct yetty_ypaint_canvas *canvas,
                                                                 struct yetty_ycore_grid_size grid_size);

//=============================================================================
// Accessors
//=============================================================================

bool yetty_ypaint_canvas_scrolling_mode(struct yetty_ypaint_canvas *canvas);

struct yetty_ycore_pixel_size yetty_ypaint_canvas_cell_get_pixel_size(struct yetty_ypaint_canvas *canvas);

struct yetty_ycore_grid_size yetty_ypaint_canvas_get_grid_size(struct yetty_ypaint_canvas *canvas);

uint32_t yetty_ypaint_canvas_line_count(struct yetty_ypaint_canvas *canvas);

//=============================================================================
// Cursor (scrolling mode only)
//=============================================================================

struct yetty_ycore_void_result yetty_ypaint_canvas_set_cursor_pos(
    struct yetty_ypaint_canvas *canvas, struct yetty_ycore_grid_cursor_pos grid_cursor_pos);

//=============================================================================
// Rolling offset (for shader uniform)
//=============================================================================

// Get rolling_row_0: absolute row of visible line 0 (pass to shader as
// row_origin). Honors any active view_top override (scrollback view), so
// the shader's coordinate frame matches what rebuild_grid laid out.
uint32_t yetty_ypaint_canvas_rolling_row_0(struct yetty_ypaint_canvas *canvas);

// The live rolling_row_0 — the canvas-line index that *would* be at the
// top of the viewport if no scrollback override were active. Use this when
// you need the live anchor (e.g. to translate a relative scroll-back
// request into an absolute view_top).
uint32_t yetty_ypaint_canvas_live_rolling_row_0(struct yetty_ypaint_canvas *canvas);

// Pin the visible viewport to an absolute canvas-line index, freezing the
// current scrollback content even as new prims keep arriving (active=true).
// Pass active=false to drop the override and resume tracking the live
// rolling_row_0. While active, scroll_lines still advances the live anchor
// in the background — it just doesn't move the visible viewport.
struct yetty_ycore_void_result yetty_ypaint_canvas_set_view_top(struct yetty_ypaint_canvas *canvas,
                                                                bool active, uint32_t view_top);

//=============================================================================
// Buffer management
//=============================================================================

struct yetty_ypaint_core_buffer;

// Add all primitives from a buffer to canvas
// Computes AABB for each primitive, tracks max_row, handles scrolling
// In scrolling mode: primitives positioned relative to cursor
// In non-scrolling mode: primitives positioned at absolute scene coordinates
struct yetty_ycore_void_result yetty_ypaint_canvas_add_buffer(
    struct yetty_ypaint_canvas *canvas, struct yetty_ypaint_core_buffer *buffer);

//=============================================================================
// Scrolling
//=============================================================================

// Scroll callback: called when primitive insertion requires scrolling
// @param user_data User data pointer
// @param num_lines Number of lines to scroll
typedef struct yetty_ycore_void_result (*yetty_ypaint_canvas_scroll_callback)(
    struct yetty_ycore_void_result *user_data, uint16_t num_lines);

// Cursor set callback: called when cursor moves WITHOUT scrolling
// @param user_data User data pointer
// @param new_row New cursor row position
typedef struct yetty_ycore_void_result (*yetty_ypaint_canvas_cursor_set_callback)(
    struct yetty_ycore_void_result *user_data, uint16_t new_row);

// Set scroll callback (called when add_buffer triggers scroll)
struct yetty_ycore_void_result yetty_ypaint_canvas_set_scroll_callback(
    struct yetty_ypaint_canvas *canvas, yetty_ypaint_canvas_scroll_callback callback,
    struct yetty_ycore_void_result *user_data);

// Set cursor callback (called when cursor moves without scroll)
struct yetty_ycore_void_result yetty_ypaint_canvas_set_cursor_callback(
    struct yetty_ypaint_canvas *canvas, yetty_ypaint_canvas_cursor_set_callback callback,
    struct yetty_ycore_void_result *user_data);

// Remove N lines from the top - primitives in those lines are deleted
// Only valid in scrolling mode
// O(1) for offset update, O(n) only for deleting the actual lines
struct yetty_ycore_void_result yetty_ypaint_canvas_scroll_lines(struct yetty_ypaint_canvas *canvas,
                                                                uint16_t num_lines);

//=============================================================================
// Packed GPU format
//=============================================================================

// Mark grid as dirty (needs rebuild)
struct yetty_ycore_void_result yetty_ypaint_canvas_mark_dirty(struct yetty_ypaint_canvas *canvas);

// Check if grid needs rebuild
bool yetty_ypaint_canvas_is_dirty(struct yetty_ypaint_canvas *canvas);

// Rebuild packed grid format for GPU upload
struct yetty_ycore_void_result yetty_ypaint_canvas_rebuild_grid(struct yetty_ypaint_canvas *canvas);

// Get packed grid data for GPU upload
const uint32_t *yetty_ypaint_canvas_grid_data(struct yetty_ypaint_canvas *canvas);
uint32_t yetty_ypaint_canvas_grid_word_count(struct yetty_ypaint_canvas *canvas);

// Clear packed grid staging
struct yetty_ycore_void_result yetty_ypaint_canvas_clear_staging(
    struct yetty_ypaint_canvas *canvas);

//=============================================================================
// Primitive staging for GPU
//=============================================================================

// Pointer + size view returned by build_prim_staging.
struct yetty_ypaint_prim_staging {
    const uint32_t *data;
    uint32_t word_count;
};
YETTY_YRESULT_DECLARE(yetty_ypaint_prim_staging, struct yetty_ypaint_prim_staging);

// Build primitive staging data for GPU upload.
// On success, returns the staging buffer view; word_count==0 on empty canvas.
struct yetty_ypaint_prim_staging_result yetty_ypaint_canvas_build_prim_staging(
    struct yetty_ypaint_canvas *canvas);

// Get total GPU size for primitives (in bytes)
uint32_t yetty_ypaint_canvas_prim_gpu_size(struct yetty_ypaint_canvas *canvas);

//=============================================================================
// State management
//=============================================================================

// Clear all lines and primitives
struct yetty_ycore_void_result yetty_ypaint_canvas_clear(struct yetty_ypaint_canvas *canvas);

// Check if canvas has any content
bool yetty_ypaint_canvas_empty(struct yetty_ypaint_canvas *canvas);

// Get total primitive count across all lines
uint32_t yetty_ypaint_canvas_primitive_count(struct yetty_ypaint_canvas *canvas);

// Get default font (for gpu resource set child inclusion)
struct yetty_ypaint_font *yetty_ypaint_canvas_get_default_font(struct yetty_ypaint_canvas *canvas);

// Number of fonts the canvas owns. Slot 0 is always the default font;
// slots 1..N-1 are the embedded fonts materialised from FONT prims (in
// the order they were first seen). The layer attaches all of these as
// children of its resource set, in this exact order, so the slot index
// here matches the dispatcher index in the merged shader.
uint32_t yetty_ypaint_canvas_font_count(const struct yetty_ypaint_canvas *canvas);

// Get font at slot. Returns NULL if slot out of range. Pointer is
// canvas-owned — callers must not destroy it.
struct yetty_ypaint_font *yetty_ypaint_canvas_get_font_at(
    const struct yetty_ypaint_canvas *canvas, uint32_t slot);

//=============================================================================
// Complex primitive access (for atlas rendering)
//=============================================================================

// Get count of visible complex prims (on visible grid lines)
uint32_t yetty_ypaint_canvas_complex_prim_count(struct yetty_ypaint_canvas *canvas);

// Get complex prim instance by index (0 to count-1)
// Returns NULL if index out of range
struct yetty_ypaint_core_complex_prim_instance;
struct yetty_ypaint_core_complex_prim_instance *yetty_ypaint_canvas_get_complex_prim(
    struct yetty_ypaint_canvas *canvas, uint32_t index);

// Get flyweight registry (for SDF ops lookup)
struct yetty_ypaint_core_flyweight_registry;
const struct yetty_ypaint_core_flyweight_registry *yetty_ypaint_canvas_get_flyweight_registry(
    struct yetty_ypaint_canvas *canvas);

// Get complex prim factory (for complex prim ops)
struct yetty_ypaint_core_complex_prim_factory;
struct yetty_ypaint_core_complex_prim_factory *yetty_ypaint_canvas_get_complex_prim_factory(
    struct yetty_ypaint_canvas *canvas);

#ifdef __cplusplus
}
#endif
