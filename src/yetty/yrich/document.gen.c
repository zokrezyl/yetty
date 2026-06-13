/* GENERATED — do not edit. */
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h> /* NULL, size_t */

struct yetty_ycore_float_result;
struct yetty_ycore_void_result;
struct yetty_yrich_operation;
struct yetty_ycore_void_result yetty_yrich_constructor(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_destroy(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_width(struct yetty_yclass_ctx *ctx,
                                                                   struct yetty_yclass_object *obj);
struct yetty_ycore_float_result yetty_yrich_document_content_height(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_render(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_apply_op(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj,
                                                             struct yetty_yrich_operation *op,
                                                             int local_flag);
struct yetty_ycore_void_result yetty_yrich_document_undo(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_redo(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_down(struct yetty_yclass_ctx *ctx,
                                                                  struct yetty_yclass_object *obj,
                                                                  float x, float y, uint32_t button,
                                                                  uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_up(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj,
                                                                float x, float y, uint32_t button,
                                                                uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_drag(struct yetty_yclass_ctx *ctx,
                                                                  struct yetty_yclass_object *obj,
                                                                  float x, float y, uint32_t button,
                                                                  uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_mouse_double_click(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, float x, float y,
    uint32_t button, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_key_down(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj,
                                                                uint32_t key, uint32_t mods);
struct yetty_ycore_void_result yetty_yrich_document_on_text_input(struct yetty_yclass_ctx *ctx,
                                                                  struct yetty_yclass_object *obj,
                                                                  struct yetty_ycore_buffer text);
typedef struct yetty_ycore_void_result (*yetty_yrich_constructor_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_destroy_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_width_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_float_result (*yetty_yrich_document_content_height_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_render_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_apply_op_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_yrich_operation *, int);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_undo_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_redo_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_down_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_up_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_drag_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_mouse_double_click_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_key_down_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yrich_document_on_text_input_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ycore_buffer);

[[maybe_unused]]
static yetty_yrich_constructor_fn yetty_yrich_document_yetty_yrich_constructor_check =
    document_constructor;
[[maybe_unused]]
static yetty_yrich_document_destroy_fn yetty_yrich_document_yetty_yrich_document_destroy_check =
    document_default_destroy;
[[maybe_unused]]
static yetty_yrich_document_content_width_fn
    yetty_yrich_document_yetty_yrich_document_content_width_check = document_default_content_width;
[[maybe_unused]]
static yetty_yrich_document_content_height_fn
    yetty_yrich_document_yetty_yrich_document_content_height_check =
        document_default_content_height;
[[maybe_unused]]
static yetty_yrich_document_render_fn yetty_yrich_document_yetty_yrich_document_render_check =
    document_default_render;
[[maybe_unused]]
static yetty_yrich_document_apply_op_fn yetty_yrich_document_yetty_yrich_document_apply_op_check =
    document_default_apply_op;
[[maybe_unused]]
static yetty_yrich_document_undo_fn yetty_yrich_document_yetty_yrich_document_undo_check =
    document_default_undo;
[[maybe_unused]]
static yetty_yrich_document_redo_fn yetty_yrich_document_yetty_yrich_document_redo_check =
    document_default_redo;
[[maybe_unused]]
static yetty_yrich_document_on_mouse_down_fn
    yetty_yrich_document_yetty_yrich_document_on_mouse_down_check = document_default_on_mouse_down;
[[maybe_unused]]
static yetty_yrich_document_on_mouse_up_fn
    yetty_yrich_document_yetty_yrich_document_on_mouse_up_check = document_default_on_mouse_up;
[[maybe_unused]]
static yetty_yrich_document_on_mouse_drag_fn
    yetty_yrich_document_yetty_yrich_document_on_mouse_drag_check = document_default_on_mouse_drag;
[[maybe_unused]]
static yetty_yrich_document_on_mouse_double_click_fn
    yetty_yrich_document_yetty_yrich_document_on_mouse_double_click_check =
        document_default_on_mouse_double_click;
[[maybe_unused]]
static yetty_yrich_document_on_key_down_fn
    yetty_yrich_document_yetty_yrich_document_on_key_down_check = document_default_on_key_down;
[[maybe_unused]]
static yetty_yrich_document_on_text_input_fn
    yetty_yrich_document_yetty_yrich_document_on_text_input_check = document_default_on_text_input;

struct yetty_yclass_ptr_result yetty_yrich_document_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_yrich_document");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yrich_document",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yrich_document),
        .data_align = _Alignof(struct yetty_yrich_document),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yrich", "constructor", (yetty_yclass_method_id_t)yetty_yrich_constructor,
         (yetty_yclass_impl_t)document_constructor},
        {"yetty_yrich", "document_destroy", (yetty_yclass_method_id_t)yetty_yrich_document_destroy,
         (yetty_yclass_impl_t)document_default_destroy},
        {"yetty_yrich", "document_content_width",
         (yetty_yclass_method_id_t)yetty_yrich_document_content_width,
         (yetty_yclass_impl_t)document_default_content_width},
        {"yetty_yrich", "document_content_height",
         (yetty_yclass_method_id_t)yetty_yrich_document_content_height,
         (yetty_yclass_impl_t)document_default_content_height},
        {"yetty_yrich", "document_render", (yetty_yclass_method_id_t)yetty_yrich_document_render,
         (yetty_yclass_impl_t)document_default_render},
        {"yetty_yrich", "document_apply_op",
         (yetty_yclass_method_id_t)yetty_yrich_document_apply_op,
         (yetty_yclass_impl_t)document_default_apply_op},
        {"yetty_yrich", "document_undo", (yetty_yclass_method_id_t)yetty_yrich_document_undo,
         (yetty_yclass_impl_t)document_default_undo},
        {"yetty_yrich", "document_redo", (yetty_yclass_method_id_t)yetty_yrich_document_redo,
         (yetty_yclass_impl_t)document_default_redo},
        {"yetty_yrich", "document_on_mouse_down",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_down,
         (yetty_yclass_impl_t)document_default_on_mouse_down},
        {"yetty_yrich", "document_on_mouse_up",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_up,
         (yetty_yclass_impl_t)document_default_on_mouse_up},
        {"yetty_yrich", "document_on_mouse_drag",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_drag,
         (yetty_yclass_impl_t)document_default_on_mouse_drag},
        {"yetty_yrich", "document_on_mouse_double_click",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_mouse_double_click,
         (yetty_yclass_impl_t)document_default_on_mouse_double_click},
        {"yetty_yrich", "document_on_key_down",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_key_down,
         (yetty_yclass_impl_t)document_default_on_key_down},
        {"yetty_yrich", "document_on_text_input",
         (yetty_yclass_method_id_t)yetty_yrich_document_on_text_input,
         (yetty_yclass_impl_t)document_default_on_text_input},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yrich_document_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yrich_document_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yrich_document_ptr_result yetty_yrich_document_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yrich_document_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_yrich_document_ptr, "yetty_yrich_document_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_yrich_document_ptr, "yetty_yrich_document_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_yrich_document_ptr, (struct yetty_yrich_document *)slice_r.value);
}

struct yetty_yclass_object *yetty_yrich_document_to(struct yetty_yrich_document *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_yrich_document_class_get();
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
