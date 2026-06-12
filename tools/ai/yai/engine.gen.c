/* GENERATED — do not edit. */
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h>  /* NULL, size_t */

struct yai_app;
struct yetty_ycore_void_result;
struct yyjson_val;
struct yetty_ycore_void_result yetty_yai_start(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app);
struct yetty_ycore_void_result yetty_yai_send_user_message(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app, const char * text);
struct yetty_ycore_void_result yetty_yai_handle_event(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app, struct yyjson_val * event);
struct yetty_ycore_void_result yetty_yai_interrupt(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app);
struct yetty_ycore_void_result yetty_yai_on_child_exit(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app, int64_t exit_status);
struct yetty_ycore_void_result yetty_yai_on_child_eof(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app);
struct yetty_ycore_void_result yetty_yai_describe_config(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app, char * out, size_t out_size);
struct yetty_ycore_void_result yetty_yai_config_knob(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app, char * out, size_t out_size);
struct yetty_ycore_void_result yetty_yai_apply_config(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app, const char * key, const char * value);
struct yetty_ycore_void_result yetty_yai_resolve_permission(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yai_app * app, int allowed);
typedef struct yetty_ycore_void_result (*yetty_yai_start_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_send_user_message_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yai_handle_event_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *, struct yyjson_val *);
typedef struct yetty_ycore_void_result (*yetty_yai_interrupt_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_exit_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *, int64_t);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_eof_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_describe_config_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *, char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yai_config_knob_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *, char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yai_apply_config_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *, const char *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yai_resolve_permission_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *, int);

[[maybe_unused]]
static yetty_yai_start_fn yetty_yai_engine_yetty_yai_start_check = engine_start;
[[maybe_unused]]
static yetty_yai_send_user_message_fn yetty_yai_engine_yetty_yai_send_user_message_check = engine_send_user_message;
[[maybe_unused]]
static yetty_yai_handle_event_fn yetty_yai_engine_yetty_yai_handle_event_check = engine_handle_event;
[[maybe_unused]]
static yetty_yai_interrupt_fn yetty_yai_engine_yetty_yai_interrupt_check = engine_interrupt;
[[maybe_unused]]
static yetty_yai_on_child_exit_fn yetty_yai_engine_yetty_yai_on_child_exit_check = engine_on_child_exit;
[[maybe_unused]]
static yetty_yai_on_child_eof_fn yetty_yai_engine_yetty_yai_on_child_eof_check = engine_on_child_eof;
[[maybe_unused]]
static yetty_yai_describe_config_fn yetty_yai_engine_yetty_yai_describe_config_check = engine_describe_config;
[[maybe_unused]]
static yetty_yai_config_knob_fn yetty_yai_engine_yetty_yai_config_knob_check = engine_config_knob;
[[maybe_unused]]
static yetty_yai_apply_config_fn yetty_yai_engine_yetty_yai_apply_config_check = engine_apply_config;
[[maybe_unused]]
static yetty_yai_resolve_permission_fn yetty_yai_engine_yetty_yai_resolve_permission_check = engine_resolve_permission;

struct yetty_yclass_ptr_result yetty_yai_engine_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yai_engine");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yai_engine",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yai_engine),
        .data_align = _Alignof(struct yetty_yai_engine),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yai", "start", (yetty_yclass_method_id_t)yetty_yai_start, (yetty_yclass_impl_t)engine_start},
        {"yetty_yai", "send_user_message", (yetty_yclass_method_id_t)yetty_yai_send_user_message, (yetty_yclass_impl_t)engine_send_user_message},
        {"yetty_yai", "handle_event", (yetty_yclass_method_id_t)yetty_yai_handle_event, (yetty_yclass_impl_t)engine_handle_event},
        {"yetty_yai", "interrupt", (yetty_yclass_method_id_t)yetty_yai_interrupt, (yetty_yclass_impl_t)engine_interrupt},
        {"yetty_yai", "on_child_exit", (yetty_yclass_method_id_t)yetty_yai_on_child_exit, (yetty_yclass_impl_t)engine_on_child_exit},
        {"yetty_yai", "on_child_eof", (yetty_yclass_method_id_t)yetty_yai_on_child_eof, (yetty_yclass_impl_t)engine_on_child_eof},
        {"yetty_yai", "describe_config", (yetty_yclass_method_id_t)yetty_yai_describe_config, (yetty_yclass_impl_t)engine_describe_config},
        {"yetty_yai", "config_knob", (yetty_yclass_method_id_t)yetty_yai_config_knob, (yetty_yclass_impl_t)engine_config_knob},
        {"yetty_yai", "apply_config", (yetty_yclass_method_id_t)yetty_yai_apply_config, (yetty_yclass_impl_t)engine_apply_config},
        {"yetty_yai", "resolve_permission", (yetty_yclass_method_id_t)yetty_yai_resolve_permission, (yetty_yclass_impl_t)engine_resolve_permission},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yai_engine_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yai_engine_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yai_engine_ptr_result yetty_yai_engine_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yai_engine_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yai_engine_ptr, "yetty_yai_engine_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yai_engine_ptr, "yetty_yai_engine_from: object_data", slice_r);
    return YETTY_OK(yetty_yai_engine_ptr, (struct yetty_yai_engine *)slice_r.value);
}

struct yetty_yclass_object *yetty_yai_engine_to(struct yetty_yai_engine *data)
{
    if (!data)
        return NULL;
    struct yetty_yclass_ptr_result class_r = yetty_yai_engine_class_get();
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
