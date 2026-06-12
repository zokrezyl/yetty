/* GENERATED — do not edit. */
#include "yetty/yfigure/figure.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h> /* NULL, size_t */

struct yetty_ycore_char_ptr_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_target;
struct yetty_yfigure_figure;
struct yetty_ywire_wire_statemachine;
struct yetty_ycore_void_result yetty_yfigure_destroy(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_render(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj,
                                                    struct yetty_ydraw_target *target);
struct yetty_ycore_void_result yetty_yfigure_constructor(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_add_child(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yetty_yfigure_figure *child,
                                                       uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_remove_child_by_id(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj,
                                                                uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_raise_child_by_id(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj,
                                                               uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_process_records(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj,
                                                             struct yetty_ycore_buffer bytes);
struct yetty_ycore_void_result yetty_yfigure_process_input(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ywire_wire_statemachine *statemachine);
struct yetty_ycore_void_result yetty_yfigure_process_bytes(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj,
                                                           const uint8_t *bytes, size_t bytes_len);
struct yetty_ycore_char_ptr_result yetty_yfigure_dump_state(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj,
                                                            int indent);
typedef struct yetty_ycore_void_result (*yetty_yfigure_destroy_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_render_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *,
                                                                  struct yetty_ydraw_target *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_constructor_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_add_child_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *,
                                                                     struct yetty_yfigure_figure *,
                                                                     uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_remove_child_by_id_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_raise_child_by_id_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_records_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_input_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *,
    struct yetty_ywire_wire_statemachine *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_bytes_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, const uint8_t *, size_t);
typedef struct yetty_ycore_char_ptr_result (*yetty_yfigure_dump_state_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int);

[[maybe_unused]]
static yetty_yfigure_destroy_fn yetty_yfigure_container_yetty_yfigure_destroy_check =
    container_destroy;
[[maybe_unused]]
static yetty_yfigure_render_fn yetty_yfigure_container_yetty_yfigure_render_check =
    container_render;
[[maybe_unused]]
static yetty_yfigure_constructor_fn yetty_yfigure_container_yetty_yfigure_constructor_check =
    yetty_yfigure_container_constructor_impl;
[[maybe_unused]]
static yetty_yfigure_add_child_fn yetty_yfigure_container_yetty_yfigure_add_child_check =
    yetty_yfigure_container_add_child_impl;
[[maybe_unused]]
static yetty_yfigure_remove_child_by_id_fn
    yetty_yfigure_container_yetty_yfigure_remove_child_by_id_check =
        yetty_yfigure_container_remove_child_by_id_impl;
[[maybe_unused]]
static yetty_yfigure_raise_child_by_id_fn
    yetty_yfigure_container_yetty_yfigure_raise_child_by_id_check =
        yetty_yfigure_container_raise_child_by_id_impl;
[[maybe_unused]]
static yetty_yfigure_process_records_fn
    yetty_yfigure_container_yetty_yfigure_process_records_check =
        yetty_yfigure_container_process_records_impl;
[[maybe_unused]]
static yetty_yfigure_process_input_fn yetty_yfigure_container_yetty_yfigure_process_input_check =
    container_process_input_slot;
[[maybe_unused]]
static yetty_yfigure_process_bytes_fn yetty_yfigure_container_yetty_yfigure_process_bytes_check =
    container_process_bytes_slot;
[[maybe_unused]]
static yetty_yfigure_dump_state_fn yetty_yfigure_container_yetty_yfigure_dump_state_check =
    container_dump_state_slot;

struct yetty_yclass_ptr_result yetty_yfigure_container_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yfigure_container");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yfigure_container",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yfigure_container),
        .data_align = _Alignof(struct yetty_yfigure_container),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy,
         (yetty_yclass_impl_t)container_destroy},
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render,
         (yetty_yclass_impl_t)container_render},
        {"yetty_yfigure", "constructor", (yetty_yclass_method_id_t)yetty_yfigure_constructor,
         (yetty_yclass_impl_t)yetty_yfigure_container_constructor_impl},
        {"yetty_yfigure", "add_child", (yetty_yclass_method_id_t)yetty_yfigure_add_child,
         (yetty_yclass_impl_t)yetty_yfigure_container_add_child_impl},
        {"yetty_yfigure", "remove_child_by_id",
         (yetty_yclass_method_id_t)yetty_yfigure_remove_child_by_id,
         (yetty_yclass_impl_t)yetty_yfigure_container_remove_child_by_id_impl},
        {"yetty_yfigure", "raise_child_by_id",
         (yetty_yclass_method_id_t)yetty_yfigure_raise_child_by_id,
         (yetty_yclass_impl_t)yetty_yfigure_container_raise_child_by_id_impl},
        {"yetty_yfigure", "process_records",
         (yetty_yclass_method_id_t)yetty_yfigure_process_records,
         (yetty_yclass_impl_t)yetty_yfigure_container_process_records_impl},
        {"yetty_yfigure", "process_input", (yetty_yclass_method_id_t)yetty_yfigure_process_input,
         (yetty_yclass_impl_t)container_process_input_slot},
        {"yetty_yfigure", "process_bytes", (yetty_yclass_method_id_t)yetty_yfigure_process_bytes,
         (yetty_yclass_impl_t)container_process_bytes_slot},
        {"yetty_yfigure", "dump_state", (yetty_yclass_method_id_t)yetty_yfigure_dump_state,
         (yetty_yclass_impl_t)container_dump_state_slot},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yfigure_container_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yfigure_container_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yfigure_container_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_yfigure_container_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yfigure_container_ptr_result yetty_yfigure_container_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yfigure_container_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yfigure_container_ptr,
                         "yetty_yfigure_container_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yfigure_container_ptr, "yetty_yfigure_container_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_yfigure_container_ptr, (struct yetty_yfigure_container *)slice_r.value);
}

struct yetty_yclass_object *yetty_yfigure_container_to(struct yetty_yfigure_container *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yfigure_container_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yetty_ycore_error_destroy(class_r.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    if (YETTY_IS_ERR(offset_r)) {
        yetty_ycore_error_destroy(offset_r.error);
        return NULL;
    }
    return (struct yetty_yclass_object *)((char *)data - offset_r.value);
}
