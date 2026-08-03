/* GENERATED — do not edit. */
/* Object API for regular class(es) `engine` (implementation module: yai).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YAI_ENGINE_H
#define YETTY_YCLASSGEN_API_YAI_ENGINE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yai_app;
struct yyjson_val;

/* The class@ annotation needs a struct to sit on; the engine carries
 * no instance state of its own (per-engine state lives in the subclass
 * data structs, shared turn state in struct yai_app). The struct's
 * size contributes 1 byte to the instance layout, which is harmless. */
struct yetty_yclass_ptr_result yetty_yai_engine_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yai_engine;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YAI_ENGINE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YAI_ENGINE_PTR_RESULT
struct yetty_yai_engine_ptr_result {
    int ok;
    union {
        struct yetty_yai_engine *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yai_engine_ptr_result yetty_yai_engine_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yai_engine_to(struct yetty_yai_engine *data);

/* Engines without a permission protocol simply have nothing to answer
 * — a no-op default, NOT an error: main.c calls this unconditionally
 * at shutdown to never leave a CLI blocked on an unanswered prompt. */
struct yetty_ycore_void_result yetty_yai_resolve_permission(struct yetty_yclass_object *obj,
                                                            struct yai_app *app, int allowed);
struct yetty_ycore_void_result yetty_yai_handle_event(struct yetty_yclass_object *obj,
                                                      struct yai_app *app,
                                                      struct yyjson_val *event);
struct yetty_ycore_void_result yetty_yai_send_user_message(struct yetty_yclass_object *obj,
                                                           struct yai_app *app, const char *text);
struct yetty_ycore_void_result yetty_yai_interrupt(struct yetty_yclass_object *obj,
                                                   struct yai_app *app);
struct yetty_ycore_void_result yetty_yai_start(struct yetty_yclass_object *obj,
                                               struct yai_app *app);
/* describe_config: write the engine-specific configuration rows
 * (model, sandbox/permission knobs, resume token semantics) into
 * `out` as newline-separated "key: value  [ENV_KNOB]" lines. The
 * caller presents them — as labels in the ygui config dialog when the
 * HUD is up, or as scrollback text without one. Errors on truncation.
 * Default: an engine with no knobs writes an empty string. */
struct yetty_ycore_void_result yetty_yai_describe_config(struct yetty_yclass_object *obj,
                                                         struct yai_app *app, char *out,
                                                         size_t out_size);
/* config_knob: the engine's ONE editable knob for the config dialog,
 * as `ENV_KEY|label|option1,option2,…|current-value`. The dialog
 * renders it as a radio group; a selection lands back in apply_config.
 * Default: no knob (empty string). */
struct yetty_ycore_void_result yetty_yai_config_knob(struct yetty_yclass_object *obj,
                                                     struct yai_app *app, char *out,
                                                     size_t out_size);
/* apply_config: a knob edited in the config dialog. `key` is the engine
 * field name from config_knob, `value` the chosen option. The value is
 * already stored in app->config by the caller (yai_config_set); the per-turn
 * engines read that struct on every spawn, so the change takes effect on the
 * next turn. The base default therefore does nothing. Engines with a live
 * protocol override this (claude pushes the new permission mode into the
 * running session immediately). */
struct yetty_ycore_void_result yetty_yai_apply_config(struct yetty_yclass_object *obj,
                                                      struct yai_app *app, const char *key,
                                                      const char *value);
struct yetty_ycore_void_result yetty_yai_on_child_exit(struct yetty_yclass_object *obj,
                                                       struct yai_app *app, int64_t exit_status);
struct yetty_ycore_void_result yetty_yai_on_child_eof(struct yetty_yclass_object *obj,
                                                      struct yai_app *app);

struct yetty_yclass_object_ptr_result yetty_yai_engine_create(struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
