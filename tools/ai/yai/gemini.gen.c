/* GENERATED — do not edit. */
#include "yetty/yai/turn-engine.h"
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h>  /* NULL, size_t */

struct yai_app;
struct yetty_ycore_void_result;
struct yyjson_val;
struct yetty_ycore_void_result yetty_yai_start(struct yetty_yclass_object * obj, struct yai_app * app);
struct yetty_ycore_void_result yetty_yai_send_user_message(struct yetty_yclass_object * obj, struct yai_app * app, const char * text);
struct yetty_ycore_void_result yetty_yai_describe_config(struct yetty_yclass_object * obj, struct yai_app * app, char * out, size_t out_size);
struct yetty_ycore_void_result yetty_yai_config_knob(struct yetty_yclass_object * obj, struct yai_app * app, char * out, size_t out_size);
struct yetty_ycore_void_result yetty_yai_handle_event(struct yetty_yclass_object * obj, struct yai_app * app, struct yyjson_val * event);
typedef struct yetty_ycore_void_result (*yetty_yai_start_fn)(struct yetty_yclass_object *, struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_send_user_message_fn)(struct yetty_yclass_object *, struct yai_app *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yai_describe_config_fn)(struct yetty_yclass_object *, struct yai_app *, char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yai_config_knob_fn)(struct yetty_yclass_object *, struct yai_app *, char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_yai_handle_event_fn)(struct yetty_yclass_object *, struct yai_app *, struct yyjson_val *);

[[maybe_unused]]
static yetty_yai_start_fn yetty_yai_gemini_yetty_yai_start_check = gemini_start;
[[maybe_unused]]
static yetty_yai_send_user_message_fn yetty_yai_gemini_yetty_yai_send_user_message_check = gemini_send_user_message;
[[maybe_unused]]
static yetty_yai_describe_config_fn yetty_yai_gemini_yetty_yai_describe_config_check = gemini_describe_config;
[[maybe_unused]]
static yetty_yai_config_knob_fn yetty_yai_gemini_yetty_yai_config_knob_check = gemini_config_knob;
[[maybe_unused]]
static yetty_yai_handle_event_fn yetty_yai_gemini_yetty_yai_handle_event_check = gemini_handle_event;

struct yetty_yclass_ptr_result yetty_yai_gemini_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yai_gemini");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yai_gemini",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yai_gemini),
        .data_align = _Alignof(struct yetty_yai_gemini),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yai", "start", (yetty_yclass_method_id_t)yetty_yai_start, (yetty_yclass_impl_t)gemini_start},
        {"yetty_yai", "send_user_message", (yetty_yclass_method_id_t)yetty_yai_send_user_message, (yetty_yclass_impl_t)gemini_send_user_message},
        {"yetty_yai", "describe_config", (yetty_yclass_method_id_t)yetty_yai_describe_config, (yetty_yclass_impl_t)gemini_describe_config},
        {"yetty_yai", "config_knob", (yetty_yclass_method_id_t)yetty_yai_config_knob, (yetty_yclass_impl_t)gemini_config_knob},
        {"yetty_yai", "handle_event", (yetty_yclass_method_id_t)yetty_yai_handle_event, (yetty_yclass_impl_t)gemini_handle_event},
    };
    struct yetty_yclass_ptr_result parent_class_r = yetty_yai_turn_engine_class_get();
    if (YETTY_IS_ERR(parent_class_r)) {
        yerror("yetty_yai_gemini_class_get: parent accessor failed: %s", parent_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yai_gemini_class_get: parent accessor failed", parent_class_r);
    }
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              parent_class_r.value, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yai_gemini_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yai_gemini_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yai_gemini_ptr_result yetty_yai_gemini_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yai_gemini_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yai_gemini_ptr, "yetty_yai_gemini_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yai_gemini_ptr, "yetty_yai_gemini_from: object_data", slice_r);
    return YETTY_OK(yetty_yai_gemini_ptr, (struct yetty_yai_gemini *)slice_r.value);
}

struct yetty_yclass_object *yetty_yai_gemini_to(struct yetty_yai_gemini *data)
{
    if (!data)
        return NULL;
    struct yetty_yclass_ptr_result class_r = yetty_yai_gemini_class_get();
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
