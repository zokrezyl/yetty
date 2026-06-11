/* GENERATED — do not edit. */
#include "yetty/yfigure/figure.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h> /* NULL, size_t */

struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_ygrid_add_record(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj,
                                                      struct yetty_ycore_buffer record);
struct yetty_ycore_void_result yetty_ygrid_clear(struct yetty_yclass_ctx *ctx,
                                                 struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygrid_destroy(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_ygrid_add_record_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_ygrid_clear_fn)(struct yetty_yclass_ctx *,
                                                               struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygrid_destroy_fn)(struct yetty_yclass_ctx *,
                                                                 struct yetty_yclass_object *);

[[maybe_unused]]
static yetty_yfigure_render_fn yetty_ygrid_grid_yetty_yfigure_render_check = ygrid_render_slot;
[[maybe_unused]]
static yetty_yfigure_destroy_fn yetty_ygrid_grid_yetty_yfigure_destroy_check = ygrid_destroy_slot;
[[maybe_unused]]
static yetty_ygrid_add_record_fn yetty_ygrid_grid_yetty_ygrid_add_record_check =
    yetty_ygrid_grid_add_record_impl;
[[maybe_unused]]
static yetty_ygrid_clear_fn yetty_ygrid_grid_yetty_ygrid_clear_check = yetty_ygrid_grid_clear_impl;
[[maybe_unused]]
static yetty_ygrid_destroy_fn yetty_ygrid_grid_yetty_ygrid_destroy_check =
    yetty_ygrid_grid_destroy_impl;
[[maybe_unused]]
static yetty_yfigure_process_bytes_fn yetty_ygrid_grid_yetty_yfigure_process_bytes_check =
    yetty_ygrid_grid_process_bytes_impl;
[[maybe_unused]]
static yetty_yfigure_reset_content_fn yetty_ygrid_grid_yetty_yfigure_reset_content_check =
    yetty_ygrid_grid_reset_content_impl;
[[maybe_unused]]
static yetty_yfigure_dump_state_fn yetty_ygrid_grid_yetty_yfigure_dump_state_check =
    yetty_ygrid_grid_dump_state_impl;
[[maybe_unused]]
static yetty_yfigure_set_scroll_fn yetty_ygrid_grid_yetty_yfigure_set_scroll_check =
    yetty_ygrid_grid_set_scroll_impl;
[[maybe_unused]]
static yetty_yfigure_set_content_size_fn yetty_ygrid_grid_yetty_yfigure_set_content_size_check =
    yetty_ygrid_grid_set_content_size_impl;

struct yetty_yclass_ptr_result yetty_ygrid_grid_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ygrid_grid");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygrid_grid",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ygrid_grid),
        .data_align = _Alignof(struct yetty_ygrid_grid),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render,
         (yetty_yclass_impl_t)ygrid_render_slot},
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy,
         (yetty_yclass_impl_t)ygrid_destroy_slot},
        {"yetty_ygrid", "add_record", (yetty_yclass_method_id_t)yetty_ygrid_add_record,
         (yetty_yclass_impl_t)yetty_ygrid_grid_add_record_impl},
        {"yetty_ygrid", "clear", (yetty_yclass_method_id_t)yetty_ygrid_clear,
         (yetty_yclass_impl_t)yetty_ygrid_grid_clear_impl},
        {"yetty_ygrid", "destroy", (yetty_yclass_method_id_t)yetty_ygrid_destroy,
         (yetty_yclass_impl_t)yetty_ygrid_grid_destroy_impl},
        {"yetty_yfigure", "process_bytes", (yetty_yclass_method_id_t)yetty_yfigure_process_bytes,
         (yetty_yclass_impl_t)yetty_ygrid_grid_process_bytes_impl},
        {"yetty_yfigure", "reset_content", (yetty_yclass_method_id_t)yetty_yfigure_reset_content,
         (yetty_yclass_impl_t)yetty_ygrid_grid_reset_content_impl},
        {"yetty_yfigure", "dump_state", (yetty_yclass_method_id_t)yetty_yfigure_dump_state,
         (yetty_yclass_impl_t)yetty_ygrid_grid_dump_state_impl},
        {"yetty_yfigure", "set_scroll", (yetty_yclass_method_id_t)yetty_yfigure_set_scroll,
         (yetty_yclass_impl_t)yetty_ygrid_grid_set_scroll_impl},
        {"yetty_yfigure", "set_content_size",
         (yetty_yclass_method_id_t)yetty_yfigure_set_content_size,
         (yetty_yclass_impl_t)yetty_ygrid_grid_set_content_size_impl},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ygrid_grid_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygrid_grid_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ygrid_grid_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygrid_grid_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygrid_grid_ptr_result yetty_ygrid_grid_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygrid_grid_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ygrid_grid_ptr, "yetty_ygrid_grid_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ygrid_grid_ptr, "yetty_ygrid_grid_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ygrid_grid_ptr, (struct yetty_ygrid_grid *)slice_r.value);
}

struct yetty_yclass_object *yetty_ygrid_grid_to(struct yetty_ygrid_grid *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_ygrid_grid_class_get();
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
