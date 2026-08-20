/* GENERATED — do not edit. */
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
struct yetty_ycore_void_result yetty_ydrawlist2_add(struct yetty_yclass_object *obj,
                                                    struct yetty_yclass_object *drawable);
struct yetty_ycore_void_result yetty_ydrawlist2_dcs_emit(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ydrawlist2_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_add_fn)(struct yetty_yclass_object *,
                                                                  struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_dcs_emit_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ydrawlist2_destroy_fn)(struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_add_fn yetty_ydrawlist2_drawable_list_yetty_ydrawlist2_add_list_add_check =
    list_add;
YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_dcs_emit_fn
    yetty_ydrawlist2_drawable_list_yetty_ydrawlist2_dcs_emit_list_dcs_emit_check = list_dcs_emit;
YETTY_MAYBE_UNUSED
static yetty_ydrawlist2_destroy_fn
    yetty_ydrawlist2_drawable_list_yetty_ydrawlist2_destroy_list_destroy_check = list_destroy;

struct yetty_yclass_ptr_result yetty_ydrawlist2_drawable_list_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ydrawlist2_drawable_list");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ydrawlist2_drawable_list",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ydrawlist2_drawable_list),
        .data_align = _Alignof(struct yetty_ydrawlist2_drawable_list),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ydrawlist2", "add", (yetty_yclass_method_id_t)yetty_ydrawlist2_add,
         (yetty_yclass_impl_t)list_add},
        {"yetty_ydrawlist2", "dcs_emit", (yetty_yclass_method_id_t)yetty_ydrawlist2_dcs_emit,
         (yetty_yclass_impl_t)list_dcs_emit},
        {"yetty_ydrawlist2", "destroy", (yetty_yclass_method_id_t)yetty_ydrawlist2_destroy,
         (yetty_yclass_impl_t)list_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ydrawlist2_drawable_list_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ydrawlist2_drawable_list_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ydrawlist2_drawable_list_ptr_result yetty_ydrawlist2_drawable_list_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ydrawlist2_drawable_list_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ydrawlist2_drawable_list_ptr,
                         "yetty_ydrawlist2_drawable_list_from: class accessor", class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ydrawlist2_drawable_list_ptr,
                         "yetty_ydrawlist2_drawable_list_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ydrawlist2_drawable_list_ptr,
                    (struct yetty_ydrawlist2_drawable_list *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_list_to(
    struct yetty_ydrawlist2_drawable_list *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ydrawlist2_drawable_list_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ydrawlist2_drawable_list_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_ydrawlist2_drawable_list_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_list_create(
    struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ydrawlist2_drawable_list_create(
    struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ydrawlist2_drawable_list");
    if (ctx && ctx->session) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ydrawlist2_drawable_list_create: remote create unsupported for a "
                         "split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ydrawlist2_drawable_list_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ydrawlist2_drawable_list_create: class accessor failed",
                         class_accessor_r);
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
struct yetty_yclass_ptr_result yetty_ydrawlist2_drawable_list_class_get(void);
struct yetty_ycore_void_result yetty_ydrawlist2_list_register(void);

/* ---- ydrawlist2_list: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ydrawlist2_list_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ydrawlist2_drawable_list") == 0) {
        return yetty_ydrawlist2_drawable_list_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ydrawlist2_list: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ydrawlist2_list_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ydrawlist2_list_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ydrawlist2_list_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
