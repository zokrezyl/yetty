/* GENERATED — do not edit. */
#include "yetty/gen/impl/yfigure/figure.h"
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h> /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h> /* calloc/free for proxy + buffer marshalling */
#include <string.h> /* memcpy/strcmp/strlen */

struct yetty_ycore_uint32_result;
struct yetty_ycore_uint64_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_registry;
struct yetty_ycore_void_result yetty_yscene_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yscene_set_registry(
    struct yetty_yclass_object *obj, struct yetty_ydraw_drawable_list_registry *registry);
struct yetty_ycore_void_result yetty_yscene_node_declare(struct yetty_yclass_object *obj,
                                                         uint64_t external_id,
                                                         uint64_t parent_external_id);
struct yetty_ycore_void_result yetty_yscene_node_set_transform(struct yetty_yclass_object *obj,
                                                               uint64_t external_id, float m00,
                                                               float m01, float m10, float m11,
                                                               float translate_x,
                                                               float translate_y);
struct yetty_ycore_void_result yetty_yscene_node_set_clip(struct yetty_yclass_object *obj,
                                                          uint64_t external_id, float min_x,
                                                          float min_y, float max_x, float max_y);
struct yetty_ycore_void_result yetty_yscene_node_clear_clip(struct yetty_yclass_object *obj,
                                                            uint64_t external_id);
struct yetty_ycore_void_result yetty_yscene_node_set_opacity(struct yetty_yclass_object *obj,
                                                             uint64_t external_id, float opacity);
struct yetty_ycore_void_result yetty_yscene_node_set_z(struct yetty_yclass_object *obj,
                                                       uint64_t external_id, int32_t paint_z);
struct yetty_ycore_void_result yetty_yscene_node_set_content(struct yetty_yclass_object *obj,
                                                             uint64_t external_id,
                                                             struct yetty_ycore_buffer content);
struct yetty_ycore_void_result yetty_yscene_node_append_batch(struct yetty_yclass_object *obj,
                                                              uint64_t external_id,
                                                              struct yetty_ycore_buffer content);
struct yetty_ycore_void_result yetty_yscene_node_replace_batch(struct yetty_yclass_object *obj,
                                                               uint64_t external_id,
                                                               uint32_t batch_index,
                                                               struct yetty_ycore_buffer content);
struct yetty_ycore_void_result yetty_yscene_node_remove_batch(struct yetty_yclass_object *obj,
                                                              uint64_t external_id,
                                                              uint32_t batch_index);
struct yetty_ycore_void_result yetty_yscene_node_delete(struct yetty_yclass_object *obj,
                                                        uint64_t external_id);
struct yetty_ycore_void_result yetty_yscene_zero(struct yetty_yclass_object *obj);
struct yetty_ycore_uint64_result yetty_yscene_commit(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yscene_layout_barrier_begin(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yscene_layout_barrier_end(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yscene_terminal_grid_generation(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yscene_terminal_grid_create(struct yetty_yclass_object *obj,
                                                                 uint32_t rows, uint32_t cols,
                                                                 float cell_width,
                                                                 float cell_height);
struct yetty_ycore_void_result yetty_yscene_terminal_grid_write(struct yetty_yclass_object *obj,
                                                                struct yetty_ycore_buffer bytes);
struct yetty_ycore_void_result yetty_yscene_terminal_write_content(struct yetty_yclass_object *obj,
                                                                   struct yetty_ycore_buffer vt,
                                                                   struct yetty_ycore_buffer rich);
struct yetty_ycore_void_result yetty_yscene_terminal_grid_resize(struct yetty_yclass_object *obj,
                                                                 uint32_t rows, uint32_t cols);
struct yetty_ycore_uint32_result yetty_yscene_terminal_reply_pending(
    struct yetty_yclass_object *obj);
struct yetty_ycore_uint64_result yetty_yscene_terminal_reply_word(struct yetty_yclass_object *obj,
                                                                  uint32_t word_index);
struct yetty_ycore_void_result yetty_yscene_terminal_reply_consume(struct yetty_yclass_object *obj,
                                                                   uint32_t byte_count);
struct yetty_ycore_uint64_result yetty_yscene_input_event_head(struct yetty_yclass_object *obj);
struct yetty_ycore_uint64_result yetty_yscene_input_event_word(struct yetty_yclass_object *obj,
                                                               uint32_t word_index);
struct yetty_ycore_void_result yetty_yscene_input_event_pop(struct yetty_yclass_object *obj);
struct yetty_ycore_uint32_result yetty_yscene_dispatch_key(struct yetty_yclass_object *obj,
                                                           uint32_t input_class,
                                                           struct yetty_ycore_buffer bytes);
struct yetty_ycore_void_result yetty_yscene_note_key_intake(struct yetty_yclass_object *obj,
                                                            uint32_t input_class,
                                                            struct yetty_ycore_buffer bytes);
struct yetty_ycore_void_result yetty_yscene_set_terminal_selection(
    struct yetty_yclass_object *obj, uint32_t start_row, uint32_t start_col, uint32_t end_row,
    uint32_t end_col, uint32_t active);
struct yetty_ycore_uint64_result yetty_yscene_dispatch_pointer(struct yetty_yclass_object *obj,
                                                               uint32_t local_x, uint32_t local_y,
                                                               uint32_t kind, uint32_t button,
                                                               uint32_t mods, uint32_t pressed);
struct yetty_ycore_void_result yetty_yscene_apply_content_transaction(
    struct yetty_yclass_object *obj, struct yetty_ycore_buffer rich);
typedef struct yetty_ycore_void_result (*yetty_yscene_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yscene_set_registry_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list_registry *);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_declare_fn)(struct yetty_yclass_object *,
                                                                       uint64_t, uint64_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_set_transform_fn)(
    struct yetty_yclass_object *, uint64_t, float, float, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_set_clip_fn)(
    struct yetty_yclass_object *, uint64_t, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_clear_clip_fn)(
    struct yetty_yclass_object *, uint64_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_set_opacity_fn)(
    struct yetty_yclass_object *, uint64_t, float);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_set_z_fn)(struct yetty_yclass_object *,
                                                                     uint64_t, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_set_content_fn)(
    struct yetty_yclass_object *, uint64_t, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_append_batch_fn)(
    struct yetty_yclass_object *, uint64_t, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_replace_batch_fn)(
    struct yetty_yclass_object *, uint64_t, uint32_t, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_remove_batch_fn)(
    struct yetty_yclass_object *, uint64_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_node_delete_fn)(struct yetty_yclass_object *,
                                                                      uint64_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_zero_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_uint64_result (*yetty_yscene_commit_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yscene_layout_barrier_begin_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yscene_layout_barrier_end_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_uint32_result (*yetty_yscene_terminal_grid_generation_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yscene_terminal_grid_create_fn)(
    struct yetty_yclass_object *, uint32_t, uint32_t, float, float);
typedef struct yetty_ycore_void_result (*yetty_yscene_terminal_grid_write_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yscene_terminal_write_content_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_buffer, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yscene_terminal_grid_resize_fn)(
    struct yetty_yclass_object *, uint32_t, uint32_t);
typedef struct yetty_ycore_uint32_result (*yetty_yscene_terminal_reply_pending_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_uint64_result (*yetty_yscene_terminal_reply_word_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_terminal_reply_consume_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_uint64_result (*yetty_yscene_input_event_head_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_uint64_result (*yetty_yscene_input_event_word_fn)(
    struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_input_event_pop_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_uint32_result (*yetty_yscene_dispatch_key_fn)(
    struct yetty_yclass_object *, uint32_t, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yscene_note_key_intake_fn)(
    struct yetty_yclass_object *, uint32_t, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yscene_set_terminal_selection_fn)(
    struct yetty_yclass_object *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef struct yetty_ycore_uint64_result (*yetty_yscene_dispatch_pointer_fn)(
    struct yetty_yclass_object *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yscene_apply_content_transaction_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_buffer);

YETTY_MAYBE_UNUSED
static yetty_yscene_constructor_fn
    yetty_yscene_scene_yetty_yscene_constructor_scene_constructor_check = scene_constructor;
YETTY_MAYBE_UNUSED
static yetty_yscene_set_registry_fn
    yetty_yscene_scene_yetty_yscene_set_registry_scene_set_registry_check = scene_set_registry;
YETTY_MAYBE_UNUSED
static yetty_yscene_node_declare_fn
    yetty_yscene_scene_yetty_yscene_node_declare_scene_node_declare_check = scene_node_declare;
YETTY_MAYBE_UNUSED
static yetty_yscene_node_set_transform_fn
    yetty_yscene_scene_yetty_yscene_node_set_transform_scene_node_set_transform_check =
        scene_node_set_transform;
YETTY_MAYBE_UNUSED
static yetty_yscene_node_set_clip_fn
    yetty_yscene_scene_yetty_yscene_node_set_clip_scene_node_set_clip_check = scene_node_set_clip;
YETTY_MAYBE_UNUSED
static yetty_yscene_node_clear_clip_fn
    yetty_yscene_scene_yetty_yscene_node_clear_clip_scene_node_clear_clip_check =
        scene_node_clear_clip;
YETTY_MAYBE_UNUSED
static yetty_yscene_node_set_opacity_fn
    yetty_yscene_scene_yetty_yscene_node_set_opacity_scene_node_set_opacity_check =
        scene_node_set_opacity;
YETTY_MAYBE_UNUSED
static yetty_yscene_node_set_z_fn
    yetty_yscene_scene_yetty_yscene_node_set_z_scene_node_set_z_check = scene_node_set_z;
YETTY_MAYBE_UNUSED
static yetty_yscene_node_set_content_fn
    yetty_yscene_scene_yetty_yscene_node_set_content_scene_node_set_content_check =
        scene_node_set_content;
YETTY_MAYBE_UNUSED
static yetty_yscene_node_append_batch_fn
    yetty_yscene_scene_yetty_yscene_node_append_batch_scene_node_append_batch_check =
        scene_node_append_batch;
YETTY_MAYBE_UNUSED
static yetty_yscene_node_replace_batch_fn
    yetty_yscene_scene_yetty_yscene_node_replace_batch_scene_node_replace_batch_check =
        scene_node_replace_batch;
YETTY_MAYBE_UNUSED
static yetty_yscene_node_remove_batch_fn
    yetty_yscene_scene_yetty_yscene_node_remove_batch_scene_node_remove_batch_check =
        scene_node_remove_batch;
YETTY_MAYBE_UNUSED
static yetty_yscene_node_delete_fn
    yetty_yscene_scene_yetty_yscene_node_delete_scene_node_delete_check = scene_node_delete;
YETTY_MAYBE_UNUSED
static yetty_yscene_zero_fn yetty_yscene_scene_yetty_yscene_zero_scene_zero_check = scene_zero;
YETTY_MAYBE_UNUSED
static yetty_yscene_commit_fn yetty_yscene_scene_yetty_yscene_commit_scene_commit_check =
    scene_commit;
YETTY_MAYBE_UNUSED
static yetty_yscene_layout_barrier_begin_fn
    yetty_yscene_scene_yetty_yscene_layout_barrier_begin_scene_layout_barrier_begin_impl_check =
        scene_layout_barrier_begin_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_layout_barrier_end_fn
    yetty_yscene_scene_yetty_yscene_layout_barrier_end_scene_layout_barrier_end_impl_check =
        scene_layout_barrier_end_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_terminal_grid_generation_fn
    yetty_yscene_scene_yetty_yscene_terminal_grid_generation_scene_terminal_grid_generation_impl_check =
        scene_terminal_grid_generation_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_terminal_grid_create_fn
    yetty_yscene_scene_yetty_yscene_terminal_grid_create_scene_terminal_grid_create_impl_check =
        scene_terminal_grid_create_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_terminal_grid_write_fn
    yetty_yscene_scene_yetty_yscene_terminal_grid_write_scene_terminal_grid_write_impl_check =
        scene_terminal_grid_write_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_terminal_write_content_fn
    yetty_yscene_scene_yetty_yscene_terminal_write_content_scene_terminal_write_content_impl_check =
        scene_terminal_write_content_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_terminal_grid_resize_fn
    yetty_yscene_scene_yetty_yscene_terminal_grid_resize_scene_terminal_grid_resize_impl_check =
        scene_terminal_grid_resize_impl;
YETTY_MAYBE_UNUSED
static yetty_yfigure_render_fn yetty_yscene_scene_yetty_yfigure_render_scene_render_slot_check =
    scene_render_slot;
YETTY_MAYBE_UNUSED
static yetty_yfigure_destroy_fn yetty_yscene_scene_yetty_yfigure_destroy_scene_destroy_slot_check =
    scene_destroy_slot;
YETTY_MAYBE_UNUSED
static yetty_yfigure_process_bytes_fn
    yetty_yscene_scene_yetty_yfigure_process_bytes_scene_process_bytes_slot_check =
        scene_process_bytes_slot;
YETTY_MAYBE_UNUSED
static yetty_yfigure_reset_content_fn
    yetty_yscene_scene_yetty_yfigure_reset_content_scene_reset_content_slot_check =
        scene_reset_content_slot;
YETTY_MAYBE_UNUSED
static yetty_yfigure_set_scroll_fn
    yetty_yscene_scene_yetty_yfigure_set_scroll_scene_set_scroll_slot_check = scene_set_scroll_slot;
YETTY_MAYBE_UNUSED
static yetty_yfigure_set_content_size_fn
    yetty_yscene_scene_yetty_yfigure_set_content_size_scene_set_content_size_slot_check =
        scene_set_content_size_slot;
YETTY_MAYBE_UNUSED
static yetty_yfigure_apply_scroll_anchor_fn
    yetty_yscene_scene_yetty_yfigure_apply_scroll_anchor_scene_apply_scroll_anchor_slot_check =
        scene_apply_scroll_anchor_slot;
YETTY_MAYBE_UNUSED
static yetty_yfigure_dump_state_fn
    yetty_yscene_scene_yetty_yfigure_dump_state_scene_dump_state_slot_check = scene_dump_state_slot;
YETTY_MAYBE_UNUSED
static yetty_yfigure_hit_opaque_fn
    yetty_yscene_scene_yetty_yfigure_hit_opaque_scene_hit_opaque_impl_check = scene_hit_opaque_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_terminal_reply_pending_fn
    yetty_yscene_scene_yetty_yscene_terminal_reply_pending_scene_terminal_reply_pending_impl_check =
        scene_terminal_reply_pending_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_terminal_reply_word_fn
    yetty_yscene_scene_yetty_yscene_terminal_reply_word_scene_terminal_reply_word_impl_check =
        scene_terminal_reply_word_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_terminal_reply_consume_fn
    yetty_yscene_scene_yetty_yscene_terminal_reply_consume_scene_terminal_reply_consume_impl_check =
        scene_terminal_reply_consume_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_input_event_head_fn
    yetty_yscene_scene_yetty_yscene_input_event_head_scene_input_event_head_impl_check =
        scene_input_event_head_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_input_event_word_fn
    yetty_yscene_scene_yetty_yscene_input_event_word_scene_input_event_word_impl_check =
        scene_input_event_word_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_input_event_pop_fn
    yetty_yscene_scene_yetty_yscene_input_event_pop_scene_input_event_pop_impl_check =
        scene_input_event_pop_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_dispatch_key_fn
    yetty_yscene_scene_yetty_yscene_dispatch_key_scene_dispatch_key_impl_check =
        scene_dispatch_key_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_note_key_intake_fn
    yetty_yscene_scene_yetty_yscene_note_key_intake_scene_note_key_intake_impl_check =
        scene_note_key_intake_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_set_terminal_selection_fn
    yetty_yscene_scene_yetty_yscene_set_terminal_selection_scene_set_terminal_selection_impl_check =
        scene_set_terminal_selection_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_dispatch_pointer_fn
    yetty_yscene_scene_yetty_yscene_dispatch_pointer_scene_dispatch_pointer_impl_check =
        scene_dispatch_pointer_impl;
YETTY_MAYBE_UNUSED
static yetty_yscene_apply_content_transaction_fn
    yetty_yscene_scene_yetty_yscene_apply_content_transaction_scene_apply_content_transaction_impl_check =
        scene_apply_content_transaction_impl;

struct yetty_yclass_ptr_result yetty_yscene_scene_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yscene_scene");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yscene_scene",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yscene_scene),
        .data_align = _Alignof(struct yetty_yscene_scene),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yscene", "constructor", (yetty_yclass_method_id_t)yetty_yscene_constructor,
         (yetty_yclass_impl_t)scene_constructor},
        {"yetty_yscene", "set_registry", (yetty_yclass_method_id_t)yetty_yscene_set_registry,
         (yetty_yclass_impl_t)scene_set_registry},
        {"yetty_yscene", "node_declare", (yetty_yclass_method_id_t)yetty_yscene_node_declare,
         (yetty_yclass_impl_t)scene_node_declare},
        {"yetty_yscene", "node_set_transform",
         (yetty_yclass_method_id_t)yetty_yscene_node_set_transform,
         (yetty_yclass_impl_t)scene_node_set_transform},
        {"yetty_yscene", "node_set_clip", (yetty_yclass_method_id_t)yetty_yscene_node_set_clip,
         (yetty_yclass_impl_t)scene_node_set_clip},
        {"yetty_yscene", "node_clear_clip", (yetty_yclass_method_id_t)yetty_yscene_node_clear_clip,
         (yetty_yclass_impl_t)scene_node_clear_clip},
        {"yetty_yscene", "node_set_opacity",
         (yetty_yclass_method_id_t)yetty_yscene_node_set_opacity,
         (yetty_yclass_impl_t)scene_node_set_opacity},
        {"yetty_yscene", "node_set_z", (yetty_yclass_method_id_t)yetty_yscene_node_set_z,
         (yetty_yclass_impl_t)scene_node_set_z},
        {"yetty_yscene", "node_set_content",
         (yetty_yclass_method_id_t)yetty_yscene_node_set_content,
         (yetty_yclass_impl_t)scene_node_set_content},
        {"yetty_yscene", "node_append_batch",
         (yetty_yclass_method_id_t)yetty_yscene_node_append_batch,
         (yetty_yclass_impl_t)scene_node_append_batch},
        {"yetty_yscene", "node_replace_batch",
         (yetty_yclass_method_id_t)yetty_yscene_node_replace_batch,
         (yetty_yclass_impl_t)scene_node_replace_batch},
        {"yetty_yscene", "node_remove_batch",
         (yetty_yclass_method_id_t)yetty_yscene_node_remove_batch,
         (yetty_yclass_impl_t)scene_node_remove_batch},
        {"yetty_yscene", "node_delete", (yetty_yclass_method_id_t)yetty_yscene_node_delete,
         (yetty_yclass_impl_t)scene_node_delete},
        {"yetty_yscene", "zero", (yetty_yclass_method_id_t)yetty_yscene_zero,
         (yetty_yclass_impl_t)scene_zero},
        {"yetty_yscene", "commit", (yetty_yclass_method_id_t)yetty_yscene_commit,
         (yetty_yclass_impl_t)scene_commit},
        {"yetty_yscene", "layout_barrier_begin",
         (yetty_yclass_method_id_t)yetty_yscene_layout_barrier_begin,
         (yetty_yclass_impl_t)scene_layout_barrier_begin_impl},
        {"yetty_yscene", "layout_barrier_end",
         (yetty_yclass_method_id_t)yetty_yscene_layout_barrier_end,
         (yetty_yclass_impl_t)scene_layout_barrier_end_impl},
        {"yetty_yscene", "terminal_grid_generation",
         (yetty_yclass_method_id_t)yetty_yscene_terminal_grid_generation,
         (yetty_yclass_impl_t)scene_terminal_grid_generation_impl},
        {"yetty_yscene", "terminal_grid_create",
         (yetty_yclass_method_id_t)yetty_yscene_terminal_grid_create,
         (yetty_yclass_impl_t)scene_terminal_grid_create_impl},
        {"yetty_yscene", "terminal_grid_write",
         (yetty_yclass_method_id_t)yetty_yscene_terminal_grid_write,
         (yetty_yclass_impl_t)scene_terminal_grid_write_impl},
        {"yetty_yscene", "terminal_write_content",
         (yetty_yclass_method_id_t)yetty_yscene_terminal_write_content,
         (yetty_yclass_impl_t)scene_terminal_write_content_impl},
        {"yetty_yscene", "terminal_grid_resize",
         (yetty_yclass_method_id_t)yetty_yscene_terminal_grid_resize,
         (yetty_yclass_impl_t)scene_terminal_grid_resize_impl},
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render,
         (yetty_yclass_impl_t)scene_render_slot},
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy,
         (yetty_yclass_impl_t)scene_destroy_slot},
        {"yetty_yfigure", "process_bytes", (yetty_yclass_method_id_t)yetty_yfigure_process_bytes,
         (yetty_yclass_impl_t)scene_process_bytes_slot},
        {"yetty_yfigure", "reset_content", (yetty_yclass_method_id_t)yetty_yfigure_reset_content,
         (yetty_yclass_impl_t)scene_reset_content_slot},
        {"yetty_yfigure", "set_scroll", (yetty_yclass_method_id_t)yetty_yfigure_set_scroll,
         (yetty_yclass_impl_t)scene_set_scroll_slot},
        {"yetty_yfigure", "set_content_size",
         (yetty_yclass_method_id_t)yetty_yfigure_set_content_size,
         (yetty_yclass_impl_t)scene_set_content_size_slot},
        {"yetty_yfigure", "apply_scroll_anchor",
         (yetty_yclass_method_id_t)yetty_yfigure_apply_scroll_anchor,
         (yetty_yclass_impl_t)scene_apply_scroll_anchor_slot},
        {"yetty_yfigure", "dump_state", (yetty_yclass_method_id_t)yetty_yfigure_dump_state,
         (yetty_yclass_impl_t)scene_dump_state_slot},
        {"yetty_yfigure", "hit_opaque", (yetty_yclass_method_id_t)yetty_yfigure_hit_opaque,
         (yetty_yclass_impl_t)scene_hit_opaque_impl},
        {"yetty_yscene", "terminal_reply_pending",
         (yetty_yclass_method_id_t)yetty_yscene_terminal_reply_pending,
         (yetty_yclass_impl_t)scene_terminal_reply_pending_impl},
        {"yetty_yscene", "terminal_reply_word",
         (yetty_yclass_method_id_t)yetty_yscene_terminal_reply_word,
         (yetty_yclass_impl_t)scene_terminal_reply_word_impl},
        {"yetty_yscene", "terminal_reply_consume",
         (yetty_yclass_method_id_t)yetty_yscene_terminal_reply_consume,
         (yetty_yclass_impl_t)scene_terminal_reply_consume_impl},
        {"yetty_yscene", "input_event_head",
         (yetty_yclass_method_id_t)yetty_yscene_input_event_head,
         (yetty_yclass_impl_t)scene_input_event_head_impl},
        {"yetty_yscene", "input_event_word",
         (yetty_yclass_method_id_t)yetty_yscene_input_event_word,
         (yetty_yclass_impl_t)scene_input_event_word_impl},
        {"yetty_yscene", "input_event_pop", (yetty_yclass_method_id_t)yetty_yscene_input_event_pop,
         (yetty_yclass_impl_t)scene_input_event_pop_impl},
        {"yetty_yscene", "dispatch_key", (yetty_yclass_method_id_t)yetty_yscene_dispatch_key,
         (yetty_yclass_impl_t)scene_dispatch_key_impl},
        {"yetty_yscene", "note_key_intake", (yetty_yclass_method_id_t)yetty_yscene_note_key_intake,
         (yetty_yclass_impl_t)scene_note_key_intake_impl},
        {"yetty_yscene", "set_terminal_selection",
         (yetty_yclass_method_id_t)yetty_yscene_set_terminal_selection,
         (yetty_yclass_impl_t)scene_set_terminal_selection_impl},
        {"yetty_yscene", "dispatch_pointer",
         (yetty_yclass_method_id_t)yetty_yscene_dispatch_pointer,
         (yetty_yclass_impl_t)scene_dispatch_pointer_impl},
        {"yetty_yscene", "apply_content_transaction",
         (yetty_yclass_method_id_t)yetty_yscene_apply_content_transaction,
         (yetty_yclass_impl_t)scene_apply_content_transaction_impl},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yscene_scene_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yscene_scene_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yscene_scene_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yscene_scene_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yscene_scene_ptr_result yetty_yscene_scene_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yscene_scene_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yscene_scene_ptr, "yetty_yscene_scene_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yscene_scene_ptr, "yetty_yscene_scene_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_yscene_scene_ptr, (struct yetty_yscene_scene *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yscene_scene_to(struct yetty_yscene_scene *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_yscene_scene_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yscene_scene_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yscene_scene_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_ycore_void_result yetty_yscene_constructor(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yscene", (yetty_yclass_method_id_t)yetty_yscene_constructor);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yscene_constructor: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yscene_constructor: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yscene_constructor: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yscene_constructor: dispatch_lookup failed");
    return ((yetty_yscene_constructor_fn)dispatch_impl_r.value)(obj);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_node_declare_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_node_declare_skel(const void *body, size_t body_len, void *resp,
                                      size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint64_t external_id;
        uint64_t parent_external_id;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_declare: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yscene_node_declare((struct yetty_yclass_object *)obj_resolve_r.value,
                                  wire_args.external_id, wire_args.parent_external_id);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_declare", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_node_set_transform_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_node_set_transform_skel(const void *body, size_t body_len, void *resp,
                                            size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint64_t external_id;
        float m00;
        float m01;
        float m10;
        float m11;
        float translate_x;
        float translate_y;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_set_transform: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yscene_node_set_transform(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.external_id, wire_args.m00,
        wire_args.m01, wire_args.m10, wire_args.m11, wire_args.translate_x, wire_args.translate_y);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_set_transform", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_node_set_clip_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_node_set_clip_skel(const void *body, size_t body_len, void *resp,
                                       size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint64_t external_id;
        float min_x;
        float min_y;
        float max_x;
        float max_y;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_set_clip: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yscene_node_set_clip(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.external_id, wire_args.min_x,
        wire_args.min_y, wire_args.max_x, wire_args.max_y);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_set_clip", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_node_clear_clip_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_node_clear_clip_skel(const void *body, size_t body_len, void *resp,
                                         size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint64_t external_id;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_clear_clip: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yscene_node_clear_clip(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.external_id);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_clear_clip", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_node_set_opacity_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_node_set_opacity_skel(const void *body, size_t body_len, void *resp,
                                          size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint64_t external_id;
        float opacity;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_set_opacity: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yscene_node_set_opacity((struct yetty_yclass_object *)obj_resolve_r.value,
                                      wire_args.external_id, wire_args.opacity);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_set_opacity", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_node_set_z_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_node_set_z_skel(const void *body, size_t body_len, void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint64_t external_id;
        int32_t paint_z;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_set_z: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yscene_node_set_z((struct yetty_yclass_object *)obj_resolve_r.value,
                                wire_args.external_id, wire_args.paint_z);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_set_z", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_node_set_content_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_node_set_content_skel(const void *body, size_t body_len, void *resp,
                                          size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint64_t external_id;
        uint32_t content_len;
    } wire_args;
#pragma pack(pop)
    if (body_len < sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.content_len) {
        return 0;
    }
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer content_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.content_len,
        .capacity = (size_t)wire_args.content_len,
    };
    body_offset += (size_t)wire_args.content_len;
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_set_content: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yscene_node_set_content(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.external_id, content_buf);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_set_content", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_node_append_batch_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_node_append_batch_skel(const void *body, size_t body_len, void *resp,
                                           size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint64_t external_id;
        uint32_t content_len;
    } wire_args;
#pragma pack(pop)
    if (body_len < sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.content_len) {
        return 0;
    }
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer content_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.content_len,
        .capacity = (size_t)wire_args.content_len,
    };
    body_offset += (size_t)wire_args.content_len;
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_append_batch: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yscene_node_append_batch(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.external_id, content_buf);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_append_batch", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_node_replace_batch_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_node_replace_batch_skel(const void *body, size_t body_len, void *resp,
                                            size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint64_t external_id;
        uint32_t batch_index;
        uint32_t content_len;
    } wire_args;
#pragma pack(pop)
    if (body_len < sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.content_len) {
        return 0;
    }
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer content_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.content_len,
        .capacity = (size_t)wire_args.content_len,
    };
    body_offset += (size_t)wire_args.content_len;
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_replace_batch: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yscene_node_replace_batch((struct yetty_yclass_object *)obj_resolve_r.value,
                                        wire_args.external_id, wire_args.batch_index, content_buf);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_replace_batch", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_node_remove_batch_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_node_remove_batch_skel(const void *body, size_t body_len, void *resp,
                                           size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint64_t external_id;
        uint32_t batch_index;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_remove_batch: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yscene_node_remove_batch((struct yetty_yclass_object *)obj_resolve_r.value,
                                       wire_args.external_id, wire_args.batch_index);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_remove_batch", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_node_delete_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_node_delete_skel(const void *body, size_t body_len, void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint64_t external_id;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_delete: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yscene_node_delete(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.external_id);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_node_delete", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_zero_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_zero_skel(const void *body, size_t body_len, void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_zero: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yscene_zero((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_zero", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_commit_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_commit_skel(const void *body, size_t body_len, void *resp, size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_commit: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_uint64_result call_r =
        yetty_yscene_commit((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_commit", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_layout_barrier_begin_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_layout_barrier_begin_skel(const void *body, size_t body_len, void *resp,
                                              size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_layout_barrier_begin: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yscene_layout_barrier_begin((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_layout_barrier_begin", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_layout_barrier_end_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_layout_barrier_end_skel(const void *body, size_t body_len, void *resp,
                                            size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_layout_barrier_end: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yscene_layout_barrier_end((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_layout_barrier_end", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_terminal_grid_generation_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_terminal_grid_generation_skel(const void *body, size_t body_len, void *resp,
                                                  size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
                                "[skel] yetty_yscene_terminal_grid_generation: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_uint32_result call_r =
        yetty_yscene_terminal_grid_generation((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_terminal_grid_generation",
                                call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_terminal_grid_create_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_terminal_grid_create_skel(const void *body, size_t body_len, void *resp,
                                              size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t rows;
        uint32_t cols;
        float cell_width;
        float cell_height;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_terminal_grid_create: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yscene_terminal_grid_create(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.rows, wire_args.cols,
        wire_args.cell_width, wire_args.cell_height);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_terminal_grid_create", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_terminal_grid_write_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_terminal_grid_write_skel(const void *body, size_t body_len, void *resp,
                                             size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t bytes_len;
    } wire_args;
#pragma pack(pop)
    if (body_len < sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.bytes_len) {
        return 0;
    }
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer bytes_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.bytes_len,
        .capacity = (size_t)wire_args.bytes_len,
    };
    body_offset += (size_t)wire_args.bytes_len;
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_terminal_grid_write: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yscene_terminal_grid_write(
        (struct yetty_yclass_object *)obj_resolve_r.value, bytes_buf);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_terminal_grid_write", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_terminal_write_content_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_terminal_write_content_skel(const void *body, size_t body_len, void *resp,
                                                size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t vt_len;
        uint32_t rich_len;
    } wire_args;
#pragma pack(pop)
    if (body_len < sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.vt_len + (size_t)wire_args.rich_len) {
        return 0;
    }
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer vt_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.vt_len,
        .capacity = (size_t)wire_args.vt_len,
    };
    body_offset += (size_t)wire_args.vt_len;
    struct yetty_ycore_buffer rich_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.rich_len,
        .capacity = (size_t)wire_args.rich_len,
    };
    body_offset += (size_t)wire_args.rich_len;
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
                                "[skel] yetty_yscene_terminal_write_content: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yscene_terminal_write_content(
        (struct yetty_yclass_object *)obj_resolve_r.value, vt_buf, rich_buf);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_terminal_write_content", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_terminal_grid_resize_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_terminal_grid_resize_skel(const void *body, size_t body_len, void *resp,
                                              size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t rows;
        uint32_t cols;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_terminal_grid_resize: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yscene_terminal_grid_resize(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.rows, wire_args.cols);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_terminal_grid_resize", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_terminal_reply_pending_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_terminal_reply_pending_skel(const void *body, size_t body_len, void *resp,
                                                size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
                                "[skel] yetty_yscene_terminal_reply_pending: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_uint32_result call_r =
        yetty_yscene_terminal_reply_pending((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_terminal_reply_pending", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_terminal_reply_word_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_terminal_reply_word_skel(const void *body, size_t body_len, void *resp,
                                             size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t word_index;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_terminal_reply_word: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_uint64_result call_r = yetty_yscene_terminal_reply_word(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.word_index);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_terminal_reply_word", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_terminal_reply_consume_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_terminal_reply_consume_skel(const void *body, size_t body_len, void *resp,
                                                size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t byte_count;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
                                "[skel] yetty_yscene_terminal_reply_consume: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yscene_terminal_reply_consume(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.byte_count);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_terminal_reply_consume", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_input_event_head_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_input_event_head_skel(const void *body, size_t body_len, void *resp,
                                          size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_input_event_head: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_uint64_result call_r =
        yetty_yscene_input_event_head((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_input_event_head", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_input_event_word_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_input_event_word_skel(const void *body, size_t body_len, void *resp,
                                          size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t word_index;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_input_event_word: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_uint64_result call_r = yetty_yscene_input_event_word(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.word_index);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_input_event_word", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_input_event_pop_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_input_event_pop_skel(const void *body, size_t body_len, void *resp,
                                         size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_input_event_pop: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r =
        yetty_yscene_input_event_pop((struct yetty_yclass_object *)obj_resolve_r.value);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_input_event_pop", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_dispatch_key_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_dispatch_key_skel(const void *body, size_t body_len, void *resp,
                                      size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t input_class;
        uint32_t bytes_len;
    } wire_args;
#pragma pack(pop)
    if (body_len < sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.bytes_len) {
        return 0;
    }
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer bytes_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.bytes_len,
        .capacity = (size_t)wire_args.bytes_len,
    };
    body_offset += (size_t)wire_args.bytes_len;
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_dispatch_key: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_uint32_result call_r = yetty_yscene_dispatch_key(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.input_class, bytes_buf);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_dispatch_key", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_note_key_intake_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_note_key_intake_skel(const void *body, size_t body_len, void *resp,
                                         size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t input_class;
        uint32_t bytes_len;
    } wire_args;
#pragma pack(pop)
    if (body_len < sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.bytes_len) {
        return 0;
    }
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer bytes_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.bytes_len,
        .capacity = (size_t)wire_args.bytes_len,
    };
    body_offset += (size_t)wire_args.bytes_len;
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_note_key_intake: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yscene_note_key_intake(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.input_class, bytes_buf);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_note_key_intake", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_set_terminal_selection_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_set_terminal_selection_skel(const void *body, size_t body_len, void *resp,
                                                size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t start_row;
        uint32_t start_col;
        uint32_t end_row;
        uint32_t end_col;
        uint32_t active;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
                                "[skel] yetty_yscene_set_terminal_selection: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yscene_set_terminal_selection(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.start_row, wire_args.start_col,
        wire_args.end_row, wire_args.end_col, wire_args.active);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_set_terminal_selection", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_dispatch_pointer_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_dispatch_pointer_skel(const void *body, size_t body_len, void *resp,
                                          size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t local_x;
        uint32_t local_y;
        uint32_t kind;
        uint32_t button;
        uint32_t mods;
        uint32_t pressed;
    } wire_args;
#pragma pack(pop)
    /* Strict length match — both sides regenerate from the same
     * annotated source; a size mismatch means signature drift, and
     * silently truncating to the local prefix would let the server
     * execute against a misaligned struct. */
    if (body_len != sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_dispatch_pointer: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_uint64_result call_r = yetty_yscene_dispatch_pointer(
        (struct yetty_yclass_object *)obj_resolve_r.value, wire_args.local_x, wire_args.local_y,
        wire_args.kind, wire_args.button, wire_args.mods, wire_args.pressed);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_dispatch_pointer", call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    if (resp_max < 1 + sizeof(call_r.value)) {
        return 0;
    }
    ((uint8_t *)resp)[0] = 0;
    memcpy((uint8_t *)resp + 1, &call_r.value, sizeof(call_r.value));
    return 1 + sizeof(call_r.value);
}

/* Signature is dictated by the yetty_yclass_rpc_skel_fn dispatch-table
 * contract (the RPC engine calls it as a fn-pointer), so it cannot return
 * a Result; handle_resolve / impl failures are absorbed into the 1-byte
 * status wire response at this boundary. External linkage so this module's
 * skel-lookup table (in the module-aggregator <stem>.gen.c) can name it
 * across translation units. */
size_t yetty_yscene_apply_content_transaction_skel(const void *, size_t, void *, size_t);
YETTY_EXTERNAL_CALLBACK
size_t yetty_yscene_apply_content_transaction_skel(const void *body, size_t body_len, void *resp,
                                                   size_t resp_max)
{
/* Byte-exact wire layout — #pragma pack matches the first-party
 * convention (yvnc/ydvnc/libvterm) and compiles on MSVC, unlike a GNU
 * packed attribute. */
#pragma pack(push, 1)
    struct {
        uint64_t obj_handle;
        uint32_t rich_len;
    } wire_args;
#pragma pack(pop)
    if (body_len < sizeof(wire_args)) {
        return 0;
    }
    memcpy(&wire_args, body, sizeof(wire_args));
    if (body_len != sizeof(wire_args) + (size_t)wire_args.rich_len) {
        return 0;
    }
    size_t body_offset = sizeof(wire_args);
    struct yetty_ycore_buffer rich_buf = {
        .data = (uint8_t *)((const uint8_t *)body + body_offset),
        .size = (size_t)wire_args.rich_len,
        .capacity = (size_t)wire_args.rich_len,
    };
    body_offset += (size_t)wire_args.rich_len;
    struct yetty_yclass_void_ptr_result obj_resolve_r =
        yetty_yclass_rpc_handle_resolve(wire_args.obj_handle);
    if (YETTY_IS_ERR(obj_resolve_r)) {
        yetty_ycore_error_print(stderr,
                                "[skel] yetty_yscene_apply_content_transaction: handle_resolve",
                                obj_resolve_r.error);
        if (resp_max < 1) {
            yetty_ycore_error_destroy(obj_resolve_r.error);
            return 0;
        }
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(obj_resolve_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(obj_resolve_r.error);
        return 1 + err_bytes;
    }
    struct yetty_ycore_void_result call_r = yetty_yscene_apply_content_transaction(
        (struct yetty_yclass_object *)obj_resolve_r.value, rich_buf);
    if (resp_max < 1) {
        return 0;
    }
    if (YETTY_IS_ERR(call_r)) {
        yetty_ycore_error_print(stderr, "[skel] yetty_yscene_apply_content_transaction",
                                call_r.error);
        ((uint8_t *)resp)[0] = 1;
        size_t err_bytes =
            yetty_ycore_error_serialize(call_r.error, (uint8_t *)resp + 1, resp_max - 1);
        yetty_ycore_error_destroy(call_r.error);
        return 1 + err_bytes;
    }
    ((uint8_t *)resp)[0] = 0;
    return 1;
}

struct yetty_ycore_void_result yetty_yscene_constructor(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yscene_scene_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yscene_scene_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yscene_scene");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_yscene_scene_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yscene_scene_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yscene_scene_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    struct yetty_ycore_void_result ctor_r = yetty_yscene_constructor(alloc_r.value);
    if (YETTY_IS_ERR(ctor_r)) {
        struct yetty_ycore_void_result free_r = yetty_yclass_object_free(alloc_r.value);
        if (YETTY_IS_ERR(free_r)) {
            yetty_ycore_error_destroy(free_r.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yscene_scene_create: constructor failed",
                         ctor_r);
    }
    return alloc_r;
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_yscene_scene_class_get(void);
size_t yetty_yscene_node_declare_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_node_set_transform_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_node_set_clip_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_node_clear_clip_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_node_set_opacity_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_node_set_z_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_node_set_content_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_node_append_batch_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_node_replace_batch_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_node_remove_batch_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_node_delete_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_zero_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_commit_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_layout_barrier_begin_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_layout_barrier_end_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_terminal_grid_generation_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_terminal_grid_create_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_terminal_grid_write_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_terminal_write_content_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_terminal_grid_resize_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_terminal_reply_pending_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_terminal_reply_word_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_terminal_reply_consume_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_input_event_head_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_input_event_word_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_input_event_pop_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_dispatch_key_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_note_key_intake_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_set_terminal_selection_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_dispatch_pointer_skel(const void *, size_t, void *, size_t);
size_t yetty_yscene_apply_content_transaction_skel(const void *, size_t, void *, size_t);
struct yetty_ycore_void_result yetty_yscene_scene_register(void);

/* ---- yscene_scene: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yscene_scene_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yscene_scene") == 0) {
        return yetty_yscene_scene_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yscene_scene: slot -> skel, name-keyed static data --------------- */

struct yetty_yscene_scene_skel_row {
    const char *name;
    yetty_yclass_rpc_skel_fn fn;
};

static const struct yetty_yscene_scene_skel_row yetty_yscene_scene_skel_rows[] = {
    {"yetty_yscene_node_declare", yetty_yscene_node_declare_skel},
    {"yetty_yscene_node_set_transform", yetty_yscene_node_set_transform_skel},
    {"yetty_yscene_node_set_clip", yetty_yscene_node_set_clip_skel},
    {"yetty_yscene_node_clear_clip", yetty_yscene_node_clear_clip_skel},
    {"yetty_yscene_node_set_opacity", yetty_yscene_node_set_opacity_skel},
    {"yetty_yscene_node_set_z", yetty_yscene_node_set_z_skel},
    {"yetty_yscene_node_set_content", yetty_yscene_node_set_content_skel},
    {"yetty_yscene_node_append_batch", yetty_yscene_node_append_batch_skel},
    {"yetty_yscene_node_replace_batch", yetty_yscene_node_replace_batch_skel},
    {"yetty_yscene_node_remove_batch", yetty_yscene_node_remove_batch_skel},
    {"yetty_yscene_node_delete", yetty_yscene_node_delete_skel},
    {"yetty_yscene_zero", yetty_yscene_zero_skel},
    {"yetty_yscene_commit", yetty_yscene_commit_skel},
    {"yetty_yscene_layout_barrier_begin", yetty_yscene_layout_barrier_begin_skel},
    {"yetty_yscene_layout_barrier_end", yetty_yscene_layout_barrier_end_skel},
    {"yetty_yscene_terminal_grid_generation", yetty_yscene_terminal_grid_generation_skel},
    {"yetty_yscene_terminal_grid_create", yetty_yscene_terminal_grid_create_skel},
    {"yetty_yscene_terminal_grid_write", yetty_yscene_terminal_grid_write_skel},
    {"yetty_yscene_terminal_write_content", yetty_yscene_terminal_write_content_skel},
    {"yetty_yscene_terminal_grid_resize", yetty_yscene_terminal_grid_resize_skel},
    {"yetty_yscene_terminal_reply_pending", yetty_yscene_terminal_reply_pending_skel},
    {"yetty_yscene_terminal_reply_word", yetty_yscene_terminal_reply_word_skel},
    {"yetty_yscene_terminal_reply_consume", yetty_yscene_terminal_reply_consume_skel},
    {"yetty_yscene_input_event_head", yetty_yscene_input_event_head_skel},
    {"yetty_yscene_input_event_word", yetty_yscene_input_event_word_skel},
    {"yetty_yscene_input_event_pop", yetty_yscene_input_event_pop_skel},
    {"yetty_yscene_dispatch_key", yetty_yscene_dispatch_key_skel},
    {"yetty_yscene_note_key_intake", yetty_yscene_note_key_intake_skel},
    {"yetty_yscene_set_terminal_selection", yetty_yscene_set_terminal_selection_skel},
    {"yetty_yscene_dispatch_pointer", yetty_yscene_dispatch_pointer_skel},
    {"yetty_yscene_apply_content_transaction", yetty_yscene_apply_content_transaction_skel},
};

/* Signature dictated by the skel-lookup hook contract; a miss is absorbed
 * into a NULL return at this boundary. */
YETTY_EXTERNAL_CALLBACK
static yetty_yclass_rpc_skel_fn yetty_yscene_scene_skel_lookup(yetty_yclass_method_slot slot)
{
    struct yetty_yclass_const_char_ptr_result slot_name_r = yetty_yclass_method_slot_name(slot);
    if (YETTY_IS_ERR(slot_name_r)) {
        yetty_ycore_error_destroy(slot_name_r.error);
        return NULL;
    }
    const char *name = slot_name_r.value;
    for (size_t i = 0;
         i < sizeof(yetty_yscene_scene_skel_rows) / sizeof(yetty_yscene_scene_skel_rows[0]); ++i) {
        if (strcmp(yetty_yscene_scene_skel_rows[i].name, name) == 0) {
            return yetty_yscene_scene_skel_rows[i].fn;
        }
    }
    return NULL;
}

/* ---- yscene_scene: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yscene_scene_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yscene_scene_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yscene_scene_register: add_accessor_lookup");
    {
        struct yetty_ycore_void_result add_skel_r =
            yetty_yclass_rpc_add_skel_lookup(yetty_yscene_scene_skel_lookup);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, add_skel_r,
                            "yetty_yscene_scene_register: rpc_add_skel_lookup");
    }
    registered = true;
    return YETTY_OK_VOID();
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_ycore_void_result yetty_yscene_scene_register(void);
struct yetty_ycore_void_result yetty_yscene_vtermgrid_register(void);
struct yetty_ycore_void_result yetty_yscene_register(void);

/* ---- yscene: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yscene_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yscene_scene_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yscene_register: submodule yscene_scene");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yscene_vtermgrid_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yscene_register: submodule yscene_vtermgrid");
    }
    registered = true;
    return YETTY_OK_VOID();
}
