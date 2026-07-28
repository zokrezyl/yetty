/* GENERATED — do not edit. */
#include "yetty/gen/impl/yapp/app.h"
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

YETTY_MAYBE_UNUSED
static yetty_yapp_init_fn yetty_ybrowser_app_yetty_yapp_init_check = ybrowser_app_init;
YETTY_MAYBE_UNUSED
static yetty_yapp_run_fn yetty_ybrowser_app_yetty_yapp_run_check = sa_worker;

struct yetty_yclass_ptr_result yetty_ybrowser_app_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ybrowser_app");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ybrowser_app",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ybrowser_app),
        .data_align = _Alignof(struct yetty_ybrowser_app),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yapp", "init", (yetty_yclass_method_id_t)yetty_yapp_init,
         (yetty_yclass_impl_t)ybrowser_app_init},
        {"yetty_yapp", "run", (yetty_yclass_method_id_t)yetty_yapp_run,
         (yetty_yclass_impl_t)sa_worker},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yapp_app_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ybrowser_app_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ybrowser_app_class_get: parent accessor failed",
                         parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ybrowser_app_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ybrowser_app_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ybrowser_app_ptr_result yetty_ybrowser_app_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ybrowser_app_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ybrowser_app_ptr, "yetty_ybrowser_app_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ybrowser_app_ptr, "yetty_ybrowser_app_from: object_data", slice_r);
    }
    return YETTY_OK(yetty_ybrowser_app_ptr, (struct yetty_ybrowser_app *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ybrowser_app_to(struct yetty_ybrowser_app *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ybrowser_app_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_ybrowser_app_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ybrowser_app_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_yclass_object_ptr_result yetty_ybrowser_app_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ybrowser_app_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ybrowser_app");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ybrowser_app_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ybrowser_app_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ybrowser_app_create: class accessor failed", class_accessor_r);
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
struct yetty_yclass_ptr_result yetty_ybrowser_app_class_get(void);
struct yetty_ycore_void_result yetty_ybrowser_register(void);

/* ---- ybrowser: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ybrowser_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ybrowser_app") == 0) {
        return yetty_ybrowser_app_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ybrowser: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ybrowser_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ybrowser_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ybrowser_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
