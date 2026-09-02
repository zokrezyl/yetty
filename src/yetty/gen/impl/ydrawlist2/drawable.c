/* GENERATED — do not edit. */
#include "yetty/gen/impl/ydrawlist2/drawable.h"
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

struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_ycore_void_result yetty_ydrawlist2_pack(struct yetty_yclass_object *obj,
                                                     struct yetty_ydraw_drawable_list *list);
struct yetty_ycore_void_result yetty_ydrawlist2_set_name(struct yetty_yclass_object *obj,
                                                         const char *name);
struct yetty_ycore_void_result yetty_ydrawlist2_set_body(struct yetty_yclass_object *obj,
                                                         const char *body);
struct yetty_ycore_void_result yetty_ydrawlist2_set_color(struct yetty_yclass_object *obj,
                                                          const char *color);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_pack_fn)(
    struct yetty_yclass_object *, struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_name_fn)(struct yetty_yclass_object *,
                                                                       const char *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_body_fn)(struct yetty_yclass_object *,
                                                                       const char *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_color_fn)(
    struct yetty_yclass_object *, const char *);

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn
    yetty_ydrawlist2_drawable_yetty_ydrawlist2_pack_drawable_pack_check = drawable_pack;

struct yetty_yclass_ptr_result yetty_ydrawlist2_drawable_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ydrawlist2_drawable");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ydrawlist2_drawable",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ydrawlist2_drawable),
        .data_align = _Alignof(struct yetty_ydrawlist2_drawable),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)drawable_pack},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ydrawlist2_drawable_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ydrawlist2_drawable_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ydrawlist2_drawable_ptr_result yetty_ydrawlist2_drawable_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ydrawlist2_drawable_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ydrawlist2_drawable_ptr,
                         "yetty_ydrawlist2_drawable_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ydrawlist2_drawable_ptr,
                         "yetty_ydrawlist2_drawable_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ydrawlist2_drawable_ptr,
                    (struct yetty_ydrawlist2_drawable *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_to(
    struct yetty_ydrawlist2_drawable *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ydrawlist2_drawable_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ydrawlist2_drawable_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_ydrawlist2_drawable_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_set_name_fn
    yetty_ydrawlist2_font_yetty_ydrawlist2_set_name_font_set_name_check = font_set_name;
YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ydrawlist2_font_yetty_ydrawlist2_pack_font_pack_check =
    font_pack;

struct yetty_yclass_ptr_result yetty_ydrawlist2_font_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ydrawlist2_font");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ydrawlist2_font",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ydrawlist2_font),
        .data_align = _Alignof(struct yetty_ydrawlist2_font),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "set_name", (yetty_yclass_method_id_t)yetty_ydrawlist2_set_name,
         (yetty_yclass_impl_t)font_set_name},
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)font_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_drawable_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ydrawlist2_font_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ydrawlist2_font_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ydrawlist2_font_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ydrawlist2_font_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ydrawlist2_font_ptr_result yetty_ydrawlist2_font_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ydrawlist2_font_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ydrawlist2_font_ptr, "yetty_ydrawlist2_font_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ydrawlist2_font_ptr, "yetty_ydrawlist2_font_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ydrawlist2_font_ptr, (struct yetty_ydrawlist2_font *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_font_to(struct yetty_ydrawlist2_font *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ydrawlist2_font_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ydrawlist2_font_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ydrawlist2_font_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_ycore_int_result yetty_ydrawlist2_font_font_id_get(struct yetty_yclass_object *obj)
{
    struct yetty_ydrawlist2_font_ptr_result data = yetty_ydrawlist2_font_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ydrawlist2_font_font_id_get: data block", data);
    }
    return YETTY_OK(yetty_ycore_int, data.value->font_id);
}

struct yetty_ycore_void_result yetty_ydrawlist2_font_font_id_set(struct yetty_yclass_object *obj,
                                                                 int32_t value)
{
    struct yetty_ydrawlist2_font_ptr_result data = yetty_ydrawlist2_font_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_font_font_id_set: data block", data);
    }
    data.value->font_id = value;
    return YETTY_OK_VOID();
}

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_set_body_fn
    yetty_ydrawlist2_text_yetty_ydrawlist2_set_body_text_set_body_check = text_set_body;
YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_set_color_fn
    yetty_ydrawlist2_text_yetty_ydrawlist2_set_color_text_set_color_check = text_set_color;
YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ydrawlist2_text_yetty_ydrawlist2_pack_text_pack_check =
    text_pack;

struct yetty_yclass_ptr_result yetty_ydrawlist2_text_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ydrawlist2_text");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ydrawlist2_text",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ydrawlist2_text),
        .data_align = _Alignof(struct yetty_ydrawlist2_text),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "set_body", (yetty_yclass_method_id_t)yetty_ydrawlist2_set_body,
         (yetty_yclass_impl_t)text_set_body},
        {"yetty_ydrawlist2", "set_color", (yetty_yclass_method_id_t)yetty_ydrawlist2_set_color,
         (yetty_yclass_impl_t)text_set_color},
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)text_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_drawable_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ydrawlist2_text_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ydrawlist2_text_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ydrawlist2_text_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ydrawlist2_text_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ydrawlist2_text_ptr_result yetty_ydrawlist2_text_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ydrawlist2_text_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ydrawlist2_text_ptr, "yetty_ydrawlist2_text_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ydrawlist2_text_ptr, "yetty_ydrawlist2_text_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ydrawlist2_text_ptr, (struct yetty_ydrawlist2_text *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_text_to(struct yetty_ydrawlist2_text *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ydrawlist2_text_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ydrawlist2_text_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ydrawlist2_text_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ydrawlist2_text_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ydrawlist2_text_ptr_result data = yetty_ydrawlist2_text_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ydrawlist2_text_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->x);
}

struct yetty_ycore_void_result yetty_ydrawlist2_text_x_set(struct yetty_yclass_object *obj,
                                                           float value)
{
    struct yetty_ydrawlist2_text_ptr_result data = yetty_ydrawlist2_text_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_text_x_set: data block", data);
    }
    data.value->x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ydrawlist2_text_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ydrawlist2_text_ptr_result data = yetty_ydrawlist2_text_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ydrawlist2_text_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->y);
}

struct yetty_ycore_void_result yetty_ydrawlist2_text_y_set(struct yetty_yclass_object *obj,
                                                           float value)
{
    struct yetty_ydrawlist2_text_ptr_result data = yetty_ydrawlist2_text_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_text_y_set: data block", data);
    }
    data.value->y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ydrawlist2_text_font_size_get(struct yetty_yclass_object *obj)
{
    struct yetty_ydrawlist2_text_ptr_result data = yetty_ydrawlist2_text_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ydrawlist2_text_font_size_get: data block", data);
    }
    return YETTY_OK(float, data.value->font_size);
}

struct yetty_ycore_void_result yetty_ydrawlist2_text_font_size_set(struct yetty_yclass_object *obj,
                                                                   float value)
{
    struct yetty_ydrawlist2_text_ptr_result data = yetty_ydrawlist2_text_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_text_font_size_set: data block", data);
    }
    data.value->font_size = value;
    return YETTY_OK_VOID();
}

struct uint32_result yetty_ydrawlist2_text_color_get(struct yetty_yclass_object *obj)
{
    struct yetty_ydrawlist2_text_ptr_result data = yetty_ydrawlist2_text_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(uint32, "yetty_ydrawlist2_text_color_get: data block", data);
    }
    return YETTY_OK(uint32, data.value->color);
}

struct yetty_ycore_void_result yetty_ydrawlist2_text_color_set(struct yetty_yclass_object *obj,
                                                               uint32_t value)
{
    struct yetty_ydrawlist2_text_ptr_result data = yetty_ydrawlist2_text_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_text_color_set: data block", data);
    }
    data.value->color = value;
    return YETTY_OK_VOID();
}

struct yetty_ycore_int_result yetty_ydrawlist2_text_layer_get(struct yetty_yclass_object *obj)
{
    struct yetty_ydrawlist2_text_ptr_result data = yetty_ydrawlist2_text_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ydrawlist2_text_layer_get: data block", data);
    }
    return YETTY_OK(yetty_ycore_int, data.value->layer);
}

struct yetty_ycore_void_result yetty_ydrawlist2_text_layer_set(struct yetty_yclass_object *obj,
                                                               int32_t value)
{
    struct yetty_ydrawlist2_text_ptr_result data = yetty_ydrawlist2_text_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_text_layer_set: data block", data);
    }
    data.value->layer = value;
    return YETTY_OK_VOID();
}

struct yetty_ycore_int_result yetty_ydrawlist2_text_font_id_get(struct yetty_yclass_object *obj)
{
    struct yetty_ydrawlist2_text_ptr_result data = yetty_ydrawlist2_text_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ydrawlist2_text_font_id_get: data block", data);
    }
    return YETTY_OK(yetty_ycore_int, data.value->font_id);
}

struct yetty_ycore_void_result yetty_ydrawlist2_text_font_id_set(struct yetty_yclass_object *obj,
                                                                 int32_t value)
{
    struct yetty_ydrawlist2_text_ptr_result data = yetty_ydrawlist2_text_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_text_font_id_set: data block", data);
    }
    data.value->font_id = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ydrawlist2_text_rotation_get(struct yetty_yclass_object *obj)
{
    struct yetty_ydrawlist2_text_ptr_result data = yetty_ydrawlist2_text_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ydrawlist2_text_rotation_get: data block", data);
    }
    return YETTY_OK(float, data.value->rotation);
}

struct yetty_ycore_void_result yetty_ydrawlist2_text_rotation_set(struct yetty_yclass_object *obj,
                                                                  float value)
{
    struct yetty_ydrawlist2_text_ptr_result data = yetty_ydrawlist2_text_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_text_rotation_set: data block", data);
    }
    data.value->rotation = value;
    return YETTY_OK_VOID();
}

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ydrawlist2_drawable");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ydrawlist2_drawable_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ydrawlist2_drawable_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ydrawlist2_drawable_create: class accessor failed",
                         class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_font_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_font_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ydrawlist2_font");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ydrawlist2_font_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ydrawlist2_font_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ydrawlist2_font_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_text_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_text_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ydrawlist2_text");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ydrawlist2_text_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ydrawlist2_text_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ydrawlist2_text_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    return alloc_r;
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_ydrawlist2_drawable_class_get(void);
struct yetty_yclass_ptr_result yetty_ydrawlist2_font_class_get(void);
struct yetty_yclass_ptr_result yetty_ydrawlist2_text_class_get(void);
struct yetty_ycore_void_result yetty_ydrawlist2_drawable_register(void);

/* ---- ydrawlist2_drawable: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ydrawlist2_drawable_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ydrawlist2_drawable") == 0) {
        return yetty_ydrawlist2_drawable_class_get();
    }
    if (strcmp(name, "yetty_ydrawlist2_font") == 0) {
        return yetty_ydrawlist2_font_class_get();
    }
    if (strcmp(name, "yetty_ydrawlist2_text") == 0) {
        return yetty_ydrawlist2_text_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ydrawlist2_drawable: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ydrawlist2_drawable_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ydrawlist2_drawable_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ydrawlist2_drawable_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_ycore_void_result yetty_ydrawlist2_drawable_register(void);
struct yetty_ycore_void_result yetty_ydrawlist2_list_register(void);
struct yetty_ycore_void_result yetty_ydrawlist2_shape_register(void);
struct yetty_ycore_void_result yetty_ydrawlist2_register(void);

/* ---- ydrawlist2: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ydrawlist2_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ydrawlist2_drawable_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ydrawlist2_register: submodule ydrawlist2_drawable");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ydrawlist2_list_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ydrawlist2_register: submodule ydrawlist2_list");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_ydrawlist2_shape_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_ydrawlist2_register: submodule ydrawlist2_shape");
    }
    registered = true;
    return YETTY_OK_VOID();
}
