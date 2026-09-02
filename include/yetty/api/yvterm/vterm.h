/* GENERATED — do not edit. */
/* Object API for regular class(es) `vterm` (implementation module: yvterm).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YVTERM_VTERM_H
#define YETTY_YCLASSGEN_API_YVTERM_VTERM_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_context;
struct yetty_ycore_buffer;
struct yetty_ycore_grid_size;
struct yetty_ycore_pixel_size;
struct yetty_ydraw_complex;
struct yetty_ywire_wire_statemachine;

typedef struct yetty_ycore_void_result (*yetty_yvterm_clear_hook_fn)(void *);
typedef struct yetty_ycore_void_result (*yetty_yvterm_materialize_fn)(
    const uint32_t *, uint32_t, const uint32_t *, uint32_t, void *, struct yetty_ydraw_complex **);
typedef struct yetty_ycore_void_result (*yetty_yvterm_reset_hook_fn)(int, void *);

struct yetty_yclass_ptr_result yetty_yvterm_vterm_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yvterm_vterm;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YVTERM_VTERM_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YVTERM_VTERM_PTR_RESULT
struct yetty_yvterm_vterm_ptr_result {
    int ok;
    union {
        struct yetty_yvterm_vterm *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yvterm_vterm_ptr_result yetty_yvterm_vterm_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yvterm_vterm_to(struct yetty_yvterm_vterm *data);

struct yetty_yclass_object_ptr_result yetty_yvterm_vterm_create(struct yetty_yclass_ctx *ctx);

/* Create the terminal-content figure for one pane. The rect is set in pixels;
 * cell_size is stored so the terminal can read it immediately after create. The
 * terminal hooks (pty write, render request, mouse subscription) are stored for
 * keyboard output and mouse-mode reporting.
 *
 * cell_size starts at a placeholder until the renderer build-out resolves exact
 * metrics from the active font; the terminal overwrites it on the first resize.
 * `context` is accepted now for that future font setup. */
struct yetty_yclass_object_ptr_result yetty_yvterm_vterm_figure_create(
    uint32_t cols, uint32_t rows, const struct yetty_context *context,
    struct yetty_yclass_object *sink);
/* Upcast to the figure base (first slice in the object). */
struct yetty_yfigure_figure_ptr_result yetty_yvterm_vterm_as_figure(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_feed(struct yetty_yclass_object *obj,
                                                       const char *bytes, size_t len);
struct yetty_ycore_void_result yetty_yvterm_vterm_resize(struct yetty_yclass_object *obj,
                                                         struct yetty_ycore_grid_size grid_size,
                                                         struct yetty_ycore_pixel_size cell_size);
/* Live display-density transition: the pane geometry path re-derives the
 * cell metrics at the viewer's NEW content scale in the same pass — this
 * updates the renderer density and the grid's retained rich density
 * TOGETHER and rebases the structural-zoom baseline by the same factor,
 * so density-driven cell growth is a unit change, never misclassified as
 * cell zoom, and the accumulated structural zoom is preserved. The
 * committed product only moves once the resize with the re-derived cells
 * lands (the push here recommits the CURRENT product). */
struct yetty_ycore_void_result yetty_yvterm_vterm_set_content_scale(struct yetty_yclass_object *obj,
                                                                    float content_scale);
struct pixel_size_result yetty_yvterm_vterm_cell_size(struct yetty_yclass_object *obj);
/* The composed grid's committed rich density product — see
 * yetty_yvterm_grid_rich_density. */
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_density(struct yetty_yclass_object *obj,
                                                               float *out_density);
struct yetty_ycore_int_result yetty_yvterm_vterm_is_dirty(struct yetty_yclass_object *obj);
/* Resolved content rect (figure-rect-local pixels): where the text surface
 * renders inside the pane. The terminal resolves the client's reservation
 * (content-rect / legacy inset envelope) against the pane and pushes the
 * result here. width/height <= 0 restores the full figure rect. */
struct yetty_ycore_void_result yetty_yvterm_vterm_set_content_rect(struct yetty_yclass_object *obj,
                                                                   float x, float y, float width,
                                                                   float height);
struct yetty_ycore_void_result yetty_yvterm_vterm_get_content_rect(struct yetty_yclass_object *obj,
                                                                   float *out_x, float *out_y,
                                                                   float *out_width,
                                                                   float *out_height);
struct yetty_ycore_void_result yetty_yvterm_vterm_set_clear_hook(struct yetty_yclass_object *obj,
                                                                 yetty_yvterm_clear_hook_fn fn,
                                                                 void *userdata);
struct yetty_ycore_void_result yetty_yvterm_vterm_set_reset_hook(struct yetty_yclass_object *obj,
                                                                 yetty_yvterm_reset_hook_fn fn,
                                                                 void *userdata);
/* Register the figure re-materialization hook on the composed grid model: the
 * terminal (which owns the complex factory) supplies a function that replays
 * a retained wire envelope into a fresh figure instance when an evicted
 * history line scrolls back into view. */
struct yetty_ycore_void_result yetty_yvterm_vterm_set_materialize(struct yetty_yclass_object *obj,
                                                                  yetty_yvterm_materialize_fn fn,
                                                                  void *userdata);
struct yetty_ycore_void_result yetty_yvterm_vterm_cursor(struct yetty_yclass_object *obj,
                                                         uint32_t *out_row, uint32_t *out_col,
                                                         uint32_t *out_visible);
struct yetty_ycore_void_result yetty_yvterm_vterm_word_bounds(struct yetty_yclass_object *obj,
                                                              uint32_t row, uint32_t col,
                                                              uint32_t *out_start_col,
                                                              uint32_t *out_end_col);
struct yetty_ycore_uint64_result yetty_yvterm_vterm_scroll_origin(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yvterm_vterm_append_primitive(
    struct yetty_yclass_object *obj, uint32_t row, const uint32_t *words, uint32_t word_count);
struct yetty_ycore_uint32_result yetty_yvterm_vterm_append_primitive_extent(
    struct yetty_yclass_object *obj, uint32_t row, const uint32_t *words, uint32_t word_count,
    float content_top_px, float content_bottom_px);
struct yetty_ycore_uint32_result yetty_yvterm_vterm_rich_group_open(struct yetty_yclass_object *obj,
                                                                    uint32_t row,
                                                                    uint64_t group_key);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_group_close(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_group_delete(struct yetty_yclass_object *obj,
                                                                    uint64_t group_key);
struct yetty_ycore_uint32_result yetty_yvterm_vterm_rich_group_query(
    struct yetty_yclass_object *obj, uint64_t group_key, uint32_t *out_span_rows);
struct yetty_ycore_uint64_result yetty_yvterm_vterm_rich_group_token(
    struct yetty_yclass_object *obj, uint64_t group_key);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_push_paint_z(struct yetty_yclass_object *obj,
                                                                    int32_t z);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_pop_paint_z(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_reset_paint_z(
    struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yvterm_vterm_paint_plan_leaf_count(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_paint_plan_leaf(
    struct yetty_yclass_object *obj, uint32_t leaf_index, uint32_t *out_block_slot,
    uint32_t *out_record_index, uint32_t *out_kind, int32_t *out_paint_z,
    uint64_t *out_paint_sequence, uint32_t *out_record_ordinal);
struct yetty_ycore_uint64_result yetty_yvterm_vterm_paint_plan_build_count(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_update_bind(struct yetty_yclass_object *obj,
                                                                   uint64_t update_key);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_group_offset_set(
    struct yetty_yclass_object *obj, uint64_t group_key, float offset_x, float offset_y);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_update_target(
    struct yetty_yclass_object *obj, uint64_t update_key, struct yetty_ydraw_complex **out_complex);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_update_journal(
    struct yetty_yclass_object *obj, uint64_t update_key, uint32_t target_field,
    const uint32_t *payload_words, uint32_t payload_word_count);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_update_journal_poison(
    struct yetty_yclass_object *obj, uint64_t update_key);
/* Borrowed handle to the composed yvterm:grid MODEL object — the
 * inspection surface (record / paint-key / stats accessors) the headless
 * ingest round-trip tests assert on. */
struct yetty_yclass_object_ptr_result yetty_yvterm_vterm_grid_object(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_update_extent_refresh(
    struct yetty_yclass_object *obj, uint64_t update_key, float content_top_px,
    float content_bottom_px);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_update_paint_z(
    struct yetty_yclass_object *obj, uint64_t update_key, int32_t *out_paint_z);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_group_reserve(
    struct yetty_yclass_object *obj, uint64_t group_key, uint32_t extra_records,
    uint32_t extra_words);
struct yetty_ycore_uint32_result yetty_yvterm_vterm_attach_complex(
    struct yetty_yclass_object *obj, uint32_t row, struct yetty_ydraw_complex *complex);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_group_clip_set(
    struct yetty_yclass_object *obj, uint64_t group_key, float clip_x, float clip_y, float clip_w,
    float clip_h);
struct yetty_ycore_uint32_result yetty_yvterm_vterm_rich_span_declare(
    struct yetty_yclass_object *obj, uint32_t span_rows);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_reserve_advance(
    struct yetty_yclass_object *obj, uint32_t advance_rows);
struct yetty_ycore_void_result yetty_yvterm_vterm_rich_batch_abort(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_relocate_rich_to_bottom(
    struct yetty_yclass_object *obj, uint32_t span_rows);
struct yetty_ycore_void_result yetty_yvterm_vterm_clear_rich_line(struct yetty_yclass_object *obj,
                                                                  uint32_t row);
struct yetty_ycore_void_result yetty_yvterm_vterm_clear_rich_all(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_register_wire(
    struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *sm);
struct yetty_ycore_int_result yetty_yvterm_vterm_on_char(struct yetty_yclass_object *obj,
                                                         uint32_t codepoint, int mods);
struct yetty_ycore_int_result yetty_yvterm_vterm_on_key(struct yetty_yclass_object *obj, int key,
                                                        int mods);
struct yetty_ycore_void_result yetty_yvterm_vterm_set_selection(struct yetty_yclass_object *obj,
                                                                int active, uint32_t anchor_row,
                                                                uint32_t anchor_col,
                                                                uint32_t head_row,
                                                                uint32_t head_col);
struct yetty_ycore_void_result yetty_yvterm_vterm_get_selection_text(
    struct yetty_yclass_object *obj, struct yetty_ycore_buffer *out);
/* Timeline index of the line at the live screen top: everything scrolled off
 * so far. A wheel-up anchors one line below this and walks toward the floor. */
struct yetty_ycore_uint64_result yetty_yvterm_vterm_get_live_anchor(
    struct yetty_yclass_object *obj);
/* Oldest timeline index still reachable across the scrollback tiers (hot ring
 * → warm lz4 segments → cold spill file). A wheel-up clamps here. With the
 * cold tier unbounded this is 0 for the whole session. */
struct yetty_ycore_uint64_result yetty_yvterm_vterm_get_scrollback_floor(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yvterm_vterm_set_view_top(struct yetty_yclass_object *obj,
                                                               int active,
                                                               uint64_t view_top_total_idx);
/* Current scrollback view state, read from the grid (the single owner). The
 * terminal's wheel driver derives every transition from this instead of
 * holding its own copy. */
struct yetty_ycore_void_result yetty_yvterm_vterm_get_view(struct yetty_yclass_object *obj,
                                                           int *out_active, uint64_t *out_view_top);
struct yetty_ycore_void_result yetty_yvterm_vterm_set_visual_zoom(struct yetty_yclass_object *obj,
                                                                  float scale, float offset_x,
                                                                  float offset_y);
/* Set the OSC-driven post-color effect for the terminal text surface. index 0
 * disables it; params are the 6 effect parameters (unused ones may be 0). */
struct yetty_ycore_void_result yetty_yvterm_vterm_set_post_effect(struct yetty_yclass_object *obj,
                                                                  uint32_t index, float p0,
                                                                  float p1, float p2, float p3,
                                                                  float p4, float p5);
/* Set the OSC-driven coordinate distortion for the terminal text surface.
 * index 0 disables it; p0/p1 are strength and radius/aspect. */
struct yetty_ycore_void_result yetty_yvterm_vterm_set_coord_effect(struct yetty_yclass_object *obj,
                                                                   uint32_t index, float p0,
                                                                   float p1, float p2, float p3,
                                                                   float p4, float p5);
/* Report the live pointer position (pane-local pixels) so mouse-following
 * effects track it. Only forces a repaint while a coordinate effect is active
 * — otherwise pointer motion must not drive frames. */
struct yetty_ycore_void_result yetty_yvterm_vterm_set_mouse(struct yetty_yclass_object *obj,
                                                            float x, float y);

#ifdef __cplusplus
}
#endif

#endif
