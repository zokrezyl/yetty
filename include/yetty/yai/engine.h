/* GENERATED — do not edit. */
/* Public interface for regular class(es) `engine` (module: yai).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YAI_ENGINE_H
#define YETTY_YCLASSGEN_YAI_ENGINE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yai_app;
struct yyjson_val;

struct yetty_yai_engine;

struct yetty_yai_engine_ptr_result {
    int ok;
    union {
        struct yetty_yai_engine *value;
        struct yetty_ycore_error error;
    };
};

struct yetty_yclass_ptr_result yetty_yai_engine_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_yai_engine_ptr_result yetty_yai_engine_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yai_engine_to(struct yetty_yai_engine *data);

struct yetty_ycore_void_result yetty_yai_resolve_permission(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj,
                                                            struct yai_app *app, int allowed);
struct yetty_ycore_void_result yetty_yai_handle_event(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj,
                                                      struct yai_app *app,
                                                      struct yyjson_val *event);
struct yetty_ycore_void_result yetty_yai_send_user_message(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj,
                                                           struct yai_app *app, const char *text);
struct yetty_ycore_void_result yetty_yai_interrupt(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj,
                                                   struct yai_app *app);
struct yetty_ycore_void_result yetty_yai_start(struct yetty_yclass_ctx *ctx,
                                               struct yetty_yclass_object *obj,
                                               struct yai_app *app);
struct yetty_ycore_void_result yetty_yai_describe_config(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj,
                                                         struct yai_app *app, char *out,
                                                         size_t out_size);
struct yetty_ycore_void_result yetty_yai_config_knob(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj,
                                                     struct yai_app *app, char *out,
                                                     size_t out_size);
struct yetty_ycore_void_result yetty_yai_apply_config(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj,
                                                      struct yai_app *app, const char *key,
                                                      const char *value);
struct yetty_ycore_void_result yetty_yai_on_child_exit(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yai_app *app, int64_t exit_status);
struct yetty_ycore_void_result yetty_yai_on_child_eof(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj,
                                                      struct yai_app *app);

typedef struct yetty_ycore_void_result (*yetty_yai_resolve_permission_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *, int);
typedef struct yetty_ycore_void_result (*yetty_yai_handle_event_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    struct yai_app *,
                                                                    struct yyjson_val *);
typedef struct yetty_ycore_void_result (*yetty_yai_send_user_message_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yai_app *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yai_interrupt_fn)(struct yetty_yclass_ctx *,
                                                                 struct yetty_yclass_object *,
                                                                 struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_start_fn)(struct yetty_yclass_ctx *,
                                                             struct yetty_yclass_object *,
                                                             struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_describe_config_fn)(struct yetty_yclass_ctx *,
                                                                       struct yetty_yclass_object *,
                                                                       struct yai_app *, char *,
                                                                       size_t);
typedef struct yetty_ycore_void_result (*yetty_yai_config_knob_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *,
                                                                   struct yai_app *, char *,
                                                                   size_t);
typedef struct yetty_ycore_void_result (*yetty_yai_apply_config_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    struct yai_app *, const char *,
                                                                    const char *);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_exit_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *,
                                                                     struct yai_app *, int64_t);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_eof_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    struct yai_app *);

struct yetty_yclass_object_ptr_result yetty_yai_engine_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yai_register(void);

#endif
