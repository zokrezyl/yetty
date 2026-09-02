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
struct yetty_ycore_void_result yetty_ydrawlist2_set_fill(struct yetty_yclass_object *obj,
                                                         const char *color);
struct yetty_ycore_void_result yetty_ydrawlist2_set_stroke(struct yetty_yclass_object *obj,
                                                           const char *color);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_fill_fn)(struct yetty_yclass_object *,
                                                                       const char *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_set_stroke_fn)(
    struct yetty_yclass_object *, const char *);

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_set_fill_fn
    yetty_ydrawlist2_shape_yetty_ydrawlist2_set_fill_shape_set_fill_check = shape_set_fill;
YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_set_stroke_fn
    yetty_ydrawlist2_shape_yetty_ydrawlist2_set_stroke_shape_set_stroke_check = shape_set_stroke;

struct yetty_yclass_ptr_result yetty_ydrawlist2_shape_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ydrawlist2_shape");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ydrawlist2_shape",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ydrawlist2_shape),
        .data_align = _Alignof(struct yetty_ydrawlist2_shape),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "set_fill", (yetty_yclass_method_id_t)yetty_ydrawlist2_set_fill,
         (yetty_yclass_impl_t)shape_set_fill},
        {"yetty_ydrawlist2", "set_stroke", (yetty_yclass_method_id_t)yetty_ydrawlist2_set_stroke,
         (yetty_yclass_impl_t)shape_set_stroke},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_drawable_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ydrawlist2_shape_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ydrawlist2_shape_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ydrawlist2_shape_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ydrawlist2_shape_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ydrawlist2_shape_ptr_result yetty_ydrawlist2_shape_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ydrawlist2_shape_ptr, "yetty_ydrawlist2_shape_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ydrawlist2_shape_ptr, "yetty_ydrawlist2_shape_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ydrawlist2_shape_ptr, (struct yetty_ydrawlist2_shape *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_shape_to(struct yetty_ydrawlist2_shape *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ydrawlist2_shape_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ydrawlist2_shape_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_ydrawlist2_shape_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct uint32_result yetty_ydrawlist2_shape_id_get(struct yetty_yclass_object *obj)
{
    struct yetty_ydrawlist2_shape_ptr_result data = yetty_ydrawlist2_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(uint32, "yetty_ydrawlist2_shape_id_get: data block", data);
    }
    return YETTY_OK(uint32, data.value->id);
}

struct yetty_ycore_void_result yetty_ydrawlist2_shape_id_set(struct yetty_yclass_object *obj,
                                                             uint32_t value)
{
    struct yetty_ydrawlist2_shape_ptr_result data = yetty_ydrawlist2_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_shape_id_set: data block", data);
    }
    data.value->id = value;
    return YETTY_OK_VOID();
}

struct yetty_ycore_int_result yetty_ydrawlist2_shape_layer_get(struct yetty_yclass_object *obj)
{
    struct yetty_ydrawlist2_shape_ptr_result data = yetty_ydrawlist2_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ydrawlist2_shape_layer_get: data block", data);
    }
    return YETTY_OK(yetty_ycore_int, data.value->layer);
}

struct yetty_ycore_void_result yetty_ydrawlist2_shape_layer_set(struct yetty_yclass_object *obj,
                                                                int32_t value)
{
    struct yetty_ydrawlist2_shape_ptr_result data = yetty_ydrawlist2_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_shape_layer_set: data block", data);
    }
    data.value->layer = value;
    return YETTY_OK_VOID();
}

struct uint32_result yetty_ydrawlist2_shape_fill_get(struct yetty_yclass_object *obj)
{
    struct yetty_ydrawlist2_shape_ptr_result data = yetty_ydrawlist2_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(uint32, "yetty_ydrawlist2_shape_fill_get: data block", data);
    }
    return YETTY_OK(uint32, data.value->fill);
}

struct yetty_ycore_void_result yetty_ydrawlist2_shape_fill_set(struct yetty_yclass_object *obj,
                                                               uint32_t value)
{
    struct yetty_ydrawlist2_shape_ptr_result data = yetty_ydrawlist2_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_shape_fill_set: data block", data);
    }
    data.value->fill = value;
    return YETTY_OK_VOID();
}

struct uint32_result yetty_ydrawlist2_shape_stroke_get(struct yetty_yclass_object *obj)
{
    struct yetty_ydrawlist2_shape_ptr_result data = yetty_ydrawlist2_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(uint32, "yetty_ydrawlist2_shape_stroke_get: data block", data);
    }
    return YETTY_OK(uint32, data.value->stroke);
}

struct yetty_ycore_void_result yetty_ydrawlist2_shape_stroke_set(struct yetty_yclass_object *obj,
                                                                 uint32_t value)
{
    struct yetty_ydrawlist2_shape_ptr_result data = yetty_ydrawlist2_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_shape_stroke_set: data block", data);
    }
    data.value->stroke = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ydrawlist2_shape_stroke_width_get(struct yetty_yclass_object *obj)
{
    struct yetty_ydrawlist2_shape_ptr_result data = yetty_ydrawlist2_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ydrawlist2_shape_stroke_width_get: data block", data);
    }
    return YETTY_OK(float, data.value->stroke_width);
}

struct yetty_ycore_void_result yetty_ydrawlist2_shape_stroke_width_set(
    struct yetty_yclass_object *obj, float value)
{
    struct yetty_ydrawlist2_shape_ptr_result data = yetty_ydrawlist2_shape_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ydrawlist2_shape_stroke_width_set: data block",
                         data);
    }
    data.value->stroke_width = value;
    return YETTY_OK_VOID();
}

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_shape_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_shape_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ydrawlist2_shape");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ydrawlist2_shape_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ydrawlist2_shape_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ydrawlist2_shape_create: class accessor failed", class_accessor_r);
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
struct yetty_yclass_ptr_result yetty_ydrawlist2_shape_class_get(void);
struct yetty_ycore_void_result yetty_ydrawlist2_shape_register(void);

/* ---- ydrawlist2_shape: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ydrawlist2_shape_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ydrawlist2_shape") == 0) {
        return yetty_ydrawlist2_shape_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ydrawlist2_shape: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ydrawlist2_shape_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ydrawlist2_shape_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ydrawlist2_shape_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
