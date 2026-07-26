/* GENERATED — do not edit. */
#include "yetty/gen/impl/yai/engine.h"
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h>  /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h>  /* calloc/free for proxy + buffer marshalling */
#include <string.h>  /* memcpy/strcmp/strlen */

struct yai_app;
struct yetty_ycore_void_result;
struct yetty_ycore_void_result yetty_yai_on_child_exit(struct yetty_yclass_object * obj, struct yai_app * app, int64_t exit_status);
struct yetty_ycore_void_result yetty_yai_on_child_eof(struct yetty_yclass_object * obj, struct yai_app * app);
struct yetty_ycore_void_result yetty_yai_interrupt(struct yetty_yclass_object * obj, struct yai_app * app);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_exit_fn)(struct yetty_yclass_object *, struct yai_app *, int64_t);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_eof_fn)(struct yetty_yclass_object *, struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_interrupt_fn)(struct yetty_yclass_object *, struct yai_app *);

YETTY_MAYBE_UNUSED
static yetty_yai_on_child_exit_fn yetty_yai_turn_engine_yetty_yai_on_child_exit_check = turn_engine_on_child_exit;
YETTY_MAYBE_UNUSED
static yetty_yai_on_child_eof_fn yetty_yai_turn_engine_yetty_yai_on_child_eof_check = turn_engine_on_child_eof;
YETTY_MAYBE_UNUSED
static yetty_yai_interrupt_fn yetty_yai_turn_engine_yetty_yai_interrupt_check = turn_engine_interrupt;

struct yetty_yclass_ptr_result yetty_yai_turn_engine_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yai_turn_engine");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yai_turn_engine",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yai_turn_engine),
        .data_align = _Alignof(struct yetty_yai_turn_engine),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yai", "on_child_exit", (yetty_yclass_method_id_t)yetty_yai_on_child_exit, (yetty_yclass_impl_t)turn_engine_on_child_exit},
        {"yetty_yai", "on_child_eof", (yetty_yclass_method_id_t)yetty_yai_on_child_eof, (yetty_yclass_impl_t)turn_engine_on_child_eof},
        {"yetty_yai", "interrupt", (yetty_yclass_method_id_t)yetty_yai_interrupt, (yetty_yclass_impl_t)turn_engine_interrupt},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yai_engine_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yai_turn_engine_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yai_turn_engine_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yai_turn_engine_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yai_turn_engine_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yai_turn_engine_ptr_result yetty_yai_turn_engine_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yai_turn_engine_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yai_turn_engine_ptr, "yetty_yai_turn_engine_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yai_turn_engine_ptr, "yetty_yai_turn_engine_from: object_data", slice_r);
    return YETTY_OK(yetty_yai_turn_engine_ptr, (struct yetty_yai_turn_engine *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yai_turn_engine_to(struct yetty_yai_turn_engine *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_yai_turn_engine_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yai_turn_engine_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yai_turn_engine_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}


struct yetty_yclass_object_ptr_result yetty_yai_turn_engine_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yai_turn_engine_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yai_turn_engine");
    if (ctx && ctx->session)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yai_turn_engine_create: remote create unsupported for a split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yai_turn_engine_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yai_turn_engine_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;
    struct yetty_yclass_object_ptr_result alloc_r =
        yetty_yclass_object_alloc(klass);
    if (YETTY_IS_ERR(alloc_r)) return alloc_r;
    return alloc_r;
}


/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_yclass_ptr_result yetty_yai_turn_engine_class_get(void);
struct yetty_ycore_void_result yetty_yai_turn_engine_register(void);

/* ---- yai_turn_engine: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yai_turn_engine_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yai_turn_engine") == 0)
        return yetty_yai_turn_engine_class_get();
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yai_turn_engine: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yai_turn_engine_register(void)
{
    static bool registered = false;
    if (registered)
        return YETTY_OK_VOID();

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yai_turn_engine_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yai_turn_engine_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}
