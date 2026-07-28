/* GENERATED — do not edit. */
#include "yetty/gen/impl/ygui/widgets/ydraw_embed.h"
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

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ygui_emit_ctx;
struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_widget_emit_body(struct yetty_yclass_object *obj,
                                                           struct yetty_ygui_emit_ctx *emit_ctx);
struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_object *obj, float x,
                                                         float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_object *obj, float x,
                                                           float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_object *obj, float x,
                                                          float y);
typedef struct yetty_ycore_void_result (*yetty_ygui_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui_destructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_ygui_widget_emit_body_fn)(
    struct yetty_yclass_object *, struct yetty_ygui_emit_ctx *);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_press_fn)(struct yetty_yclass_object *,
                                                                       float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_release_fn)(
    struct yetty_yclass_object *, float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_motion_fn)(
    struct yetty_yclass_object *, float, float);

YETTY_MAYBE_UNUSED
static yetty_ygui_constructor_fn yetty_ygui_yrich_view_yetty_ygui_constructor_check =
    yrich_view_ctor;
YETTY_MAYBE_UNUSED
static yetty_ygui_destructor_fn yetty_ygui_yrich_view_yetty_ygui_destructor_check = yrich_view_dtor;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_emit_body_fn yetty_ygui_yrich_view_yetty_ygui_widget_emit_body_check =
    yrich_view_emit_body;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_on_press_fn yetty_ygui_yrich_view_yetty_ygui_widget_on_press_check =
    yrich_view_on_press;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_on_release_fn yetty_ygui_yrich_view_yetty_ygui_widget_on_release_check =
    yrich_view_on_release;
YETTY_MAYBE_UNUSED
static yetty_ygui_widget_on_motion_fn yetty_ygui_yrich_view_yetty_ygui_widget_on_motion_check =
    yrich_view_on_motion;

struct yetty_yclass_ptr_result yetty_ygui_yrich_view_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ygui_yrich_view");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ygui_yrich_view",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ygui_yrich_view),
        .data_align = _Alignof(struct yetty_ygui_yrich_view),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ygui", "constructor", (yetty_yclass_method_id_t)yetty_ygui_constructor,
         (yetty_yclass_impl_t)yrich_view_ctor},
        {"yetty_ygui", "destructor", (yetty_yclass_method_id_t)yetty_ygui_destructor,
         (yetty_yclass_impl_t)yrich_view_dtor},
        {"yetty_ygui", "widget_emit_body", (yetty_yclass_method_id_t)yetty_ygui_widget_emit_body,
         (yetty_yclass_impl_t)yrich_view_emit_body},
        {"yetty_ygui", "widget_on_press", (yetty_yclass_method_id_t)yetty_ygui_widget_on_press,
         (yetty_yclass_impl_t)yrich_view_on_press},
        {"yetty_ygui", "widget_on_release", (yetty_yclass_method_id_t)yetty_ygui_widget_on_release,
         (yetty_yclass_impl_t)yrich_view_on_release},
        {"yetty_ygui", "widget_on_motion", (yetty_yclass_method_id_t)yetty_ygui_widget_on_motion,
         (yetty_yclass_impl_t)yrich_view_on_motion},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_ygui_ydraw_embed_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_ygui_yrich_view_class_get: parent accessor failed: %s",
               parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ygui_yrich_view_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r = yetty_yclass_register(
        &desc, ops, sizeof(ops) / sizeof(ops[0]), parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ygui_yrich_view_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_ygui_yrich_view_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ygui_yrich_view_ptr_result yetty_ygui_yrich_view_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ygui_yrich_view_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ygui_yrich_view_ptr, "yetty_ygui_yrich_view_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ygui_yrich_view_ptr, "yetty_ygui_yrich_view_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ygui_yrich_view_ptr, (struct yetty_ygui_yrich_view *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ygui_yrich_view_to(struct yetty_ygui_yrich_view *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ygui_yrich_view_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ygui_yrich_view_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_ygui_yrich_view_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_yrich_view_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ygui_yrich_view_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ygui_yrich_view");
    if (ctx && ctx->session) {
        return YETTY_ERR(
            yetty_yclass_object_ptr,
            "yetty_ygui_yrich_view_create: remote create unsupported for a split-mode class; "
            "wrap a server handle via yetty_yclass_object_proxy_create");
    }
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ygui_yrich_view_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yrich_view_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) {
        return alloc_r;
    }
    struct yetty_ycore_void_result ctor_r = yetty_ygui_constructor(alloc_r.value);
    if (YETTY_IS_ERR(ctor_r)) {
        struct yetty_ycore_void_result free_r = yetty_yclass_object_free(alloc_r.value);
        if (YETTY_IS_ERR(free_r)) {
            yetty_ycore_error_destroy(free_r.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ygui_yrich_view_create: constructor failed", ctor_r);
    }
    return alloc_r;
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_ygui_yrich_view_class_get(void);
struct yetty_ycore_void_result yetty_ygui_yrich_view_register(void);

/* ---- ygui_yrich_view: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_ygui_yrich_view_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_ygui_yrich_view") == 0) {
        return yetty_ygui_yrich_view_class_get();
    }
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- ygui_yrich_view: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_ygui_yrich_view_register(void)
{
    static bool registered = false;
    if (registered) {
        return YETTY_OK_VOID();
    }

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_ygui_yrich_view_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_ygui_yrich_view_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
