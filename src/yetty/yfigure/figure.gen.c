/* GENERATED — do not edit. */
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

struct yetty_ycore_char_ptr_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_target;
struct yetty_ywire_wire_statemachine;
struct yetty_ycore_void_result yetty_yfigure_render(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj,
                                                    struct yetty_ydraw_target *target);
struct yetty_ycore_void_result yetty_yfigure_destroy(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_process_input(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ywire_wire_statemachine *statemachine);
struct yetty_ycore_void_result yetty_yfigure_process_bytes(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj,
                                                           const uint8_t *bytes, size_t bytes_len);
struct yetty_ycore_void_result yetty_yfigure_reset_content(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj);
struct yetty_ycore_char_ptr_result yetty_yfigure_dump_state(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj,
                                                            int indent);
struct yetty_ycore_void_result yetty_yfigure_set_scroll(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *obj,
                                                        float scroll_x, float scroll_y);
struct yetty_ycore_void_result yetty_yfigure_set_content_size(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              float content_w, float content_h);
typedef struct yetty_ycore_void_result (*yetty_yfigure_render_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *,
                                                                  struct yetty_ydraw_target *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_destroy_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_input_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *,
    struct yetty_ywire_wire_statemachine *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_bytes_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, const uint8_t *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_reset_content_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_char_ptr_result (*yetty_yfigure_dump_state_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_scroll_fn)(struct yetty_yclass_ctx *,
                                                                      struct yetty_yclass_object *,
                                                                      float, float);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_content_size_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);

[[maybe_unused]]
static yetty_yfigure_render_fn yetty_yfigure_figure_yetty_yfigure_render_check =
    yetty_yfigure_figure_default_render;
[[maybe_unused]]
static yetty_yfigure_destroy_fn yetty_yfigure_figure_yetty_yfigure_destroy_check =
    yetty_yfigure_figure_default_destroy;
[[maybe_unused]]
static yetty_yfigure_process_input_fn yetty_yfigure_figure_yetty_yfigure_process_input_check =
    yetty_yfigure_figure_default_process_input;
[[maybe_unused]]
static yetty_yfigure_process_bytes_fn yetty_yfigure_figure_yetty_yfigure_process_bytes_check =
    yetty_yfigure_figure_default_process_bytes;
[[maybe_unused]]
static yetty_yfigure_reset_content_fn yetty_yfigure_figure_yetty_yfigure_reset_content_check =
    yetty_yfigure_figure_default_reset_content;
[[maybe_unused]]
static yetty_yfigure_dump_state_fn yetty_yfigure_figure_yetty_yfigure_dump_state_check =
    yetty_yfigure_figure_default_dump_state;
[[maybe_unused]]
static yetty_yfigure_set_scroll_fn yetty_yfigure_figure_yetty_yfigure_set_scroll_check =
    yetty_yfigure_figure_default_set_scroll;
[[maybe_unused]]
static yetty_yfigure_set_content_size_fn yetty_yfigure_figure_yetty_yfigure_set_content_size_check =
    yetty_yfigure_figure_default_set_content_size;

struct yetty_yclass_ptr_result yetty_yfigure_figure_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yfigure_figure");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yfigure_figure",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yfigure_figure),
        .data_align = _Alignof(struct yetty_yfigure_figure),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yfigure", "render", (yetty_yclass_method_id_t)yetty_yfigure_render,
         (yetty_yclass_impl_t)yetty_yfigure_figure_default_render},
        {"yetty_yfigure", "destroy", (yetty_yclass_method_id_t)yetty_yfigure_destroy,
         (yetty_yclass_impl_t)yetty_yfigure_figure_default_destroy},
        {"yetty_yfigure", "process_input", (yetty_yclass_method_id_t)yetty_yfigure_process_input,
         (yetty_yclass_impl_t)yetty_yfigure_figure_default_process_input},
        {"yetty_yfigure", "process_bytes", (yetty_yclass_method_id_t)yetty_yfigure_process_bytes,
         (yetty_yclass_impl_t)yetty_yfigure_figure_default_process_bytes},
        {"yetty_yfigure", "reset_content", (yetty_yclass_method_id_t)yetty_yfigure_reset_content,
         (yetty_yclass_impl_t)yetty_yfigure_figure_default_reset_content},
        {"yetty_yfigure", "dump_state", (yetty_yclass_method_id_t)yetty_yfigure_dump_state,
         (yetty_yclass_impl_t)yetty_yfigure_figure_default_dump_state},
        {"yetty_yfigure", "set_scroll", (yetty_yclass_method_id_t)yetty_yfigure_set_scroll,
         (yetty_yclass_impl_t)yetty_yfigure_figure_default_set_scroll},
        {"yetty_yfigure", "set_content_size",
         (yetty_yclass_method_id_t)yetty_yfigure_set_content_size,
         (yetty_yclass_impl_t)yetty_yfigure_figure_default_set_content_size},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yfigure_figure_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yfigure_figure_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yfigure_figure_ptr_result yetty_yfigure_figure_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "yetty_yfigure_figure_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yfigure_figure_ptr, "yetty_yfigure_figure_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_yfigure_figure_ptr, (struct yetty_yfigure_figure *)slice_r.value);
}

struct yetty_yclass_object *yetty_yfigure_figure_to(struct yetty_yfigure_figure *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yfigure_figure_class_get();
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

struct rectangle_result yetty_yfigure_figure_rect_get(struct yetty_yclass_object *obj)
{
    struct yetty_yfigure_figure_ptr_result data = yetty_yfigure_figure_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(rectangle, "yetty_yfigure_figure_rect_get: data block", data);
    }
    return YETTY_OK(rectangle, data.value->rect);
}

struct yetty_ycore_void_result yetty_yfigure_figure_rect_set(struct yetty_yclass_object *obj,
                                                             struct yetty_ycore_rectangle value)
{
    struct yetty_yfigure_figure_ptr_result data = yetty_yfigure_figure_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_figure_rect_set: data block", data);
    }
    data.value->rect = value;
    return YETTY_OK_VOID();
}

struct yetty_ycore_int_result yetty_yfigure_figure_z_get(struct yetty_yclass_object *obj)
{
    struct yetty_yfigure_figure_ptr_result data = yetty_yfigure_figure_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_int, "yetty_yfigure_figure_z_get: data block", data);
    }
    return YETTY_OK(yetty_ycore_int, data.value->z);
}

struct yetty_ycore_void_result yetty_yfigure_figure_z_set(struct yetty_yclass_object *obj,
                                                          int value)
{
    struct yetty_yfigure_figure_ptr_result data = yetty_yfigure_figure_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_figure_z_set: data block", data);
    }
    data.value->z = value;
    return YETTY_OK_VOID();
}

struct yetty_ycore_int_result yetty_yfigure_figure_hidden_get(struct yetty_yclass_object *obj)
{
    struct yetty_yfigure_figure_ptr_result data = yetty_yfigure_figure_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_int, "yetty_yfigure_figure_hidden_get: data block", data);
    }
    return YETTY_OK(yetty_ycore_int, data.value->hidden);
}

struct yetty_ycore_void_result yetty_yfigure_figure_hidden_set(struct yetty_yclass_object *obj,
                                                               int value)
{
    struct yetty_yfigure_figure_ptr_result data = yetty_yfigure_figure_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_figure_hidden_set: data block", data);
    }
    data.value->hidden = value;
    return YETTY_OK_VOID();
}

struct yetty_ycore_int_result yetty_yfigure_figure_dirty_get(struct yetty_yclass_object *obj)
{
    struct yetty_yfigure_figure_ptr_result data = yetty_yfigure_figure_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_int, "yetty_yfigure_figure_dirty_get: data block", data);
    }
    return YETTY_OK(yetty_ycore_int, data.value->dirty);
}

struct yetty_ycore_void_result yetty_yfigure_figure_dirty_set(struct yetty_yclass_object *obj,
                                                              int value)
{
    struct yetty_yfigure_figure_ptr_result data = yetty_yfigure_figure_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_figure_dirty_set: data block", data);
    }
    data.value->dirty = value;
    return YETTY_OK_VOID();
}

struct yetty_ycore_int_result yetty_yfigure_figure_absolute_coords_get(
    struct yetty_yclass_object *obj)
{
    struct yetty_yfigure_figure_ptr_result data = yetty_yfigure_figure_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_int, "yetty_yfigure_figure_absolute_coords_get: data block",
                         data);
    }
    return YETTY_OK(yetty_ycore_int, data.value->absolute_coords);
}

struct yetty_ycore_void_result yetty_yfigure_figure_absolute_coords_set(
    struct yetty_yclass_object *obj, int value)
{
    struct yetty_yfigure_figure_ptr_result data = yetty_yfigure_figure_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yfigure_figure_absolute_coords_set: data block",
                         data);
    }
    data.value->absolute_coords = value;
    return YETTY_OK_VOID();
}
