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
struct yetty_ycore_void_result yetty_ycomplex2_set_glb(struct yetty_yclass_object *obj,
                                                       const char *path);
typedef struct yetty_ycore_void_result (*yetty_ycomplex2_set_glb_fn)(struct yetty_yclass_object *,
                                                                     const char *);

YETTY_MAYBE_UNUSED
static yetty_ycomplex2_set_glb_fn yetty_ycomplex2_mesh_yetty_ycomplex2_set_glb_mesh_set_glb_check =
    mesh_set_glb;
YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_pack_fn yetty_ycomplex2_mesh_yetty_ydrawlist2_pack_mesh_pack_check =
    mesh_pack;

struct yetty_yclass_ptr_result yetty_ycomplex2_mesh_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ycomplex2_mesh");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ycomplex2_mesh",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ycomplex2_mesh),
        .data_align = _Alignof(struct yetty_ycomplex2_mesh),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ycomplex2", "set_glb", (yetty_yclass_method_id_t)yetty_ycomplex2_set_glb,
         (yetty_yclass_impl_t)mesh_set_glb},
        {"yetty_ydrawlist2", "pack", (yetty_yclass_method_id_t)yetty_ydrawlist2_pack,
         (yetty_yclass_impl_t)mesh_pack},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ydrawlist2_drawable_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ycomplex2_mesh_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ycomplex2_mesh_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ycomplex2_mesh_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ycomplex2_mesh_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ycomplex2_mesh_ptr_result yetty_ycomplex2_mesh_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ycomplex2_mesh_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ycomplex2_mesh_ptr, "yetty_ycomplex2_mesh_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ycomplex2_mesh_ptr, "yetty_ycomplex2_mesh_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ycomplex2_mesh_ptr, (struct yetty_ycomplex2_mesh *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ycomplex2_mesh_to(struct yetty_ycomplex2_mesh *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ycomplex2_mesh_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ycomplex2_mesh_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ycomplex2_mesh_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct float_result yetty_ycomplex2_mesh_x_get(struct yetty_yclass_object *obj)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ycomplex2_mesh_x_get: data block", data);
    }
    return YETTY_OK(float, data.value->x);
}

struct yetty_ycore_void_result yetty_ycomplex2_mesh_x_set(struct yetty_yclass_object *obj,
                                                          float value)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ycomplex2_mesh_x_set: data block", data);
    }
    data.value->x = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ycomplex2_mesh_y_get(struct yetty_yclass_object *obj)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ycomplex2_mesh_y_get: data block", data);
    }
    return YETTY_OK(float, data.value->y);
}

struct yetty_ycore_void_result yetty_ycomplex2_mesh_y_set(struct yetty_yclass_object *obj,
                                                          float value)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ycomplex2_mesh_y_set: data block", data);
    }
    data.value->y = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ycomplex2_mesh_width_get(struct yetty_yclass_object *obj)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ycomplex2_mesh_width_get: data block", data);
    }
    return YETTY_OK(float, data.value->width);
}

struct yetty_ycore_void_result yetty_ycomplex2_mesh_width_set(struct yetty_yclass_object *obj,
                                                              float value)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ycomplex2_mesh_width_set: data block", data);
    }
    data.value->width = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ycomplex2_mesh_height_get(struct yetty_yclass_object *obj)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ycomplex2_mesh_height_get: data block", data);
    }
    return YETTY_OK(float, data.value->height);
}

struct yetty_ycore_void_result yetty_ycomplex2_mesh_height_set(struct yetty_yclass_object *obj,
                                                               float value)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ycomplex2_mesh_height_set: data block", data);
    }
    data.value->height = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ycomplex2_mesh_azimuth_get(struct yetty_yclass_object *obj)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ycomplex2_mesh_azimuth_get: data block", data);
    }
    return YETTY_OK(float, data.value->azimuth);
}

struct yetty_ycore_void_result yetty_ycomplex2_mesh_azimuth_set(struct yetty_yclass_object *obj,
                                                                float value)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ycomplex2_mesh_azimuth_set: data block", data);
    }
    data.value->azimuth = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ycomplex2_mesh_elevation_get(struct yetty_yclass_object *obj)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ycomplex2_mesh_elevation_get: data block", data);
    }
    return YETTY_OK(float, data.value->elevation);
}

struct yetty_ycore_void_result yetty_ycomplex2_mesh_elevation_set(struct yetty_yclass_object *obj,
                                                                  float value)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ycomplex2_mesh_elevation_set: data block", data);
    }
    data.value->elevation = value;
    return YETTY_OK_VOID();
}

struct float_result yetty_ycomplex2_mesh_zoom_get(struct yetty_yclass_object *obj)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(float, "yetty_ycomplex2_mesh_zoom_get: data block", data);
    }
    return YETTY_OK(float, data.value->zoom);
}

struct yetty_ycore_void_result yetty_ycomplex2_mesh_zoom_set(struct yetty_yclass_object *obj,
                                                             float value)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ycomplex2_mesh_zoom_set: data block", data);
    }
    data.value->zoom = value;
    return YETTY_OK_VOID();
}

struct uint32_result yetty_ycomplex2_mesh_wireframe_get(struct yetty_yclass_object *obj)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(uint32, "yetty_ycomplex2_mesh_wireframe_get: data block", data);
    }
    return YETTY_OK(uint32, data.value->wireframe);
}

struct yetty_ycore_void_result yetty_ycomplex2_mesh_wireframe_set(struct yetty_yclass_object *obj,
                                                                  uint32_t value)
{
    struct yetty_ycomplex2_mesh_ptr_result data = yetty_ycomplex2_mesh_from(obj);
    if (YETTY_IS_ERR(data)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ycomplex2_mesh_wireframe_set: data block", data);
    }
    data.value->wireframe = value;
    return YETTY_OK_VOID();
}

struct yetty_yclass_object_ptr_result yetty_ycomplex2_mesh_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ycomplex2_mesh_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ycomplex2_mesh");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ycomplex2_mesh_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ycomplex2_mesh_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ycomplex2_mesh_create: class accessor failed", class_accessor_r);
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
struct yetty_yclass_ptr_result yetty_ycomplex2_mesh_class_get(void);
struct yetty_ycore_void_result yetty_ycomplex2_mesh_register(void);

/* ---- ycomplex2_mesh: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ycomplex2_mesh_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ycomplex2_mesh") == 0) {
        return yetty_ycomplex2_mesh_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ycomplex2_mesh: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ycomplex2_mesh_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ycomplex2_mesh_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ycomplex2_mesh_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
