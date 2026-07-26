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
struct yyjson_val;
struct yetty_ycore_void_result yetty_yai_resolve_permission(struct yetty_yclass_object * obj, struct yai_app * app, int allowed);
struct yetty_ycore_void_result yetty_yai_handle_event(struct yetty_yclass_object * obj, struct yai_app * app, struct yyjson_val * event);
struct yetty_ycore_void_result yetty_yai_send_user_message(struct yetty_yclass_object * obj, struct yai_app * app, const char * text);
struct yetty_ycore_void_result yetty_yai_interrupt(struct yetty_yclass_object * obj, struct yai_app * app);
struct yetty_ycore_void_result yetty_yai_start(struct yetty_yclass_object * obj, struct yai_app * app);
struct yetty_ycore_void_result yetty_yai_describe_config(struct yetty_yclass_object * obj, struct yai_app * app, char * out, size_t out_size);
struct yetty_ycore_void_result yetty_yai_config_knob(struct yetty_yclass_object * obj, struct yai_app * app, char * out, size_t out_size);
struct yetty_ycore_void_result yetty_yai_apply_config(struct yetty_yclass_object * obj, struct yai_app * app, const char * key, const char * value);
struct yetty_ycore_void_result yetty_yai_on_child_exit(struct yetty_yclass_object * obj, struct yai_app * app, int64_t exit_status);
struct yetty_ycore_void_result yetty_yai_on_child_eof(struct yetty_yclass_object * obj, struct yai_app * app);
typedef struct yetty_ycore_void_result (*yetty_yai_resolve_permission_fn)(struct yetty_yclass_object *, struct yai_app *, int);
typedef struct yetty_ycore_void_result (*yetty_yai_handle_event_fn)(struct yetty_yclass_object *, struct yai_app *, struct yyjson_val *);
typedef struct yetty_ycore_void_result (*yetty_yai_send_user_message_fn)(struct yetty_yclass_object *, struct yai_app *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yai_interrupt_fn)(struct yetty_yclass_object *, struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_start_fn)(struct yetty_yclass_object *, struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_describe_config_fn)(struct yetty_yclass_object *, struct yai_app *, char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yai_config_knob_fn)(struct yetty_yclass_object *, struct yai_app *, char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yai_apply_config_fn)(struct yetty_yclass_object *, struct yai_app *, const char *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_exit_fn)(struct yetty_yclass_object *, struct yai_app *, int64_t);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_eof_fn)(struct yetty_yclass_object *, struct yai_app *);

YETTY_MAYBE_UNUSED
static yetty_yai_resolve_permission_fn yetty_yai_claude_yetty_yai_resolve_permission_check = claude_resolve_permission;
YETTY_MAYBE_UNUSED
static yetty_yai_handle_event_fn yetty_yai_claude_yetty_yai_handle_event_check = claude_handle_event;
YETTY_MAYBE_UNUSED
static yetty_yai_send_user_message_fn yetty_yai_claude_yetty_yai_send_user_message_check = claude_send_user_message;
YETTY_MAYBE_UNUSED
static yetty_yai_interrupt_fn yetty_yai_claude_yetty_yai_interrupt_check = claude_interrupt;
YETTY_MAYBE_UNUSED
static yetty_yai_start_fn yetty_yai_claude_yetty_yai_start_check = claude_start;
YETTY_MAYBE_UNUSED
static yetty_yai_describe_config_fn yetty_yai_claude_yetty_yai_describe_config_check = claude_describe_config;
YETTY_MAYBE_UNUSED
static yetty_yai_config_knob_fn yetty_yai_claude_yetty_yai_config_knob_check = claude_config_knob;
YETTY_MAYBE_UNUSED
static yetty_yai_apply_config_fn yetty_yai_claude_yetty_yai_apply_config_check = claude_apply_config;
YETTY_MAYBE_UNUSED
static yetty_yai_on_child_exit_fn yetty_yai_claude_yetty_yai_on_child_exit_check = claude_on_child_exit;
YETTY_MAYBE_UNUSED
static yetty_yai_on_child_eof_fn yetty_yai_claude_yetty_yai_on_child_eof_check = claude_on_child_eof;

struct yetty_yclass_ptr_result yetty_yai_claude_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yai_claude");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yai_claude",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yai_claude),
        .data_align = _Alignof(struct yetty_yai_claude),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yai", "resolve_permission", (yetty_yclass_method_id_t)yetty_yai_resolve_permission, (yetty_yclass_impl_t)claude_resolve_permission},
        {"yetty_yai", "handle_event", (yetty_yclass_method_id_t)yetty_yai_handle_event, (yetty_yclass_impl_t)claude_handle_event},
        {"yetty_yai", "send_user_message", (yetty_yclass_method_id_t)yetty_yai_send_user_message, (yetty_yclass_impl_t)claude_send_user_message},
        {"yetty_yai", "interrupt", (yetty_yclass_method_id_t)yetty_yai_interrupt, (yetty_yclass_impl_t)claude_interrupt},
        {"yetty_yai", "start", (yetty_yclass_method_id_t)yetty_yai_start, (yetty_yclass_impl_t)claude_start},
        {"yetty_yai", "describe_config", (yetty_yclass_method_id_t)yetty_yai_describe_config, (yetty_yclass_impl_t)claude_describe_config},
        {"yetty_yai", "config_knob", (yetty_yclass_method_id_t)yetty_yai_config_knob, (yetty_yclass_impl_t)claude_config_knob},
        {"yetty_yai", "apply_config", (yetty_yclass_method_id_t)yetty_yai_apply_config, (yetty_yclass_impl_t)claude_apply_config},
        {"yetty_yai", "on_child_exit", (yetty_yclass_method_id_t)yetty_yai_on_child_exit, (yetty_yclass_impl_t)claude_on_child_exit},
        {"yetty_yai", "on_child_eof", (yetty_yclass_method_id_t)yetty_yai_on_child_eof, (yetty_yclass_impl_t)claude_on_child_eof},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yai_engine_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yai_claude_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yai_claude_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yai_claude_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yai_claude_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yai_claude_ptr_result yetty_yai_claude_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yai_claude_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yai_claude_ptr, "yetty_yai_claude_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yai_claude_ptr, "yetty_yai_claude_from: object_data", slice_r);
    return YETTY_OK(yetty_yai_claude_ptr, (struct yetty_yai_claude *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_yai_claude_to(struct yetty_yai_claude *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_yai_claude_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yai_claude_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yai_claude_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}


struct yetty_yclass_object_ptr_result yetty_yai_claude_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yai_claude_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yai_claude");
    if (ctx && ctx->session)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yai_claude_create: remote create unsupported for a split-mode class; "
                         "wrap a server handle via yetty_yclass_object_proxy_create");
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yai_claude_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yai_claude_create: class accessor failed", class_accessor_r);
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
struct yetty_yclass_ptr_result yetty_yai_claude_class_get(void);
struct yetty_ycore_void_result yetty_yai_claude_register(void);

/* ---- yai_claude: class name -> accessor (lazy) ---------------------- */

static struct yetty_yclass_ptr_result yetty_yai_claude_accessor_lookup(const char *name)
{
    if (strcmp(name, "yetty_yai_claude") == 0)
        return yetty_yai_claude_class_get();
    /* "Not mine": OK with NULL value -- yetty_yclass_by_name walks to next hook. */
    return YETTY_OK(yetty_yclass_ptr, NULL);
}

/* ---- yai_claude: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yai_claude_register(void)
{
    static bool registered = false;
    if (registered)
        return YETTY_OK_VOID();

    struct yetty_ycore_void_result add_accessor_r =
        yetty_yclass_add_accessor_lookup(yetty_yai_claude_accessor_lookup);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, add_accessor_r,
                        "yetty_yai_claude_register: add_accessor_lookup");
    registered = true;
    return YETTY_OK_VOID();
}

/* Forward decls. A class tagged platform@<x> is registered only on
 * that platform: its accessor/skel decls and its registration entry
 * are wrapped in #ifdef YETTY_PLATFORM_<X>, where CMake compiles the
 * class .c. A cross-platform class is a plain strong ref, defined in
 * the same library and pulled in when register() is. Submodule
 * registers are chained as strong externs (always co-linked). */
struct yetty_ycore_void_result yetty_yai_claude_register(void);
struct yetty_ycore_void_result yetty_yai_codex_register(void);
struct yetty_ycore_void_result yetty_yai_editor_register(void);
struct yetty_ycore_void_result yetty_yai_editor_emacs_register(void);
struct yetty_ycore_void_result yetty_yai_editor_vi_register(void);
struct yetty_ycore_void_result yetty_yai_engine_register(void);
struct yetty_ycore_void_result yetty_yai_gemini_register(void);
struct yetty_ycore_void_result yetty_yai_turn_engine_register(void);
struct yetty_ycore_void_result yetty_yai_register(void);


/* ---- yai: explicit yclass-RPC hook registration ------------- */

struct yetty_ycore_void_result yetty_yai_register(void)
{
    static bool registered = false;
    if (registered)
        return YETTY_OK_VOID();

    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yai_claude_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yai_register: submodule yai_claude");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yai_codex_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yai_register: submodule yai_codex");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yai_editor_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yai_register: submodule yai_editor");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yai_editor_emacs_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yai_register: submodule yai_editor_emacs");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yai_editor_vi_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yai_register: submodule yai_editor_vi");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yai_engine_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yai_register: submodule yai_engine");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yai_gemini_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yai_register: submodule yai_gemini");
    }
    {
        /* Submodule aggregator is always compiled into the same
         * library, so this strong call is always resolved. */
        struct yetty_ycore_void_result sub_r = yetty_yai_turn_engine_register();
        YETTY_RETURN_IF_ERR(yetty_ycore_void, sub_r,
                            "yetty_yai_register: submodule yai_turn_engine");
    }
    registered = true;
    return YETTY_OK_VOID();
}
