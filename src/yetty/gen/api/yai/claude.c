/* GENERATED — do not edit. */
#include <yetty/api/yai/claude.h>

#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h> /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h> /* malloc/free for buffer marshalling */
#include <string.h> /* memcpy/strlen */

struct yai_app;
struct yetty_ycore_void_result;
struct yyjson_val;
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
struct yetty_ycore_void_result yetty_yai_describe_config(struct yetty_yclass_object *obj,
                                                         struct yai_app *app, char *out,
                                                         size_t out_size);
struct yetty_ycore_void_result yetty_yai_config_knob(struct yetty_yclass_object *obj,
                                                     struct yai_app *app, char *out,
                                                     size_t out_size);
struct yetty_ycore_void_result yetty_yai_apply_config(struct yetty_yclass_object *obj,
                                                      struct yai_app *app, const char *key,
                                                      const char *value);
struct yetty_ycore_void_result yetty_yai_on_child_exit(struct yetty_yclass_object *obj,
                                                       struct yai_app *app, int64_t exit_status);
struct yetty_ycore_void_result yetty_yai_on_child_eof(struct yetty_yclass_object *obj,
                                                      struct yai_app *app);
typedef struct yetty_ycore_void_result (*yetty_yai_resolve_permission_fn)(
    struct yetty_yclass_object *, struct yai_app *, int);
typedef struct yetty_ycore_void_result (*yetty_yai_handle_event_fn)(struct yetty_yclass_object *,
                                                                    struct yai_app *,
                                                                    struct yyjson_val *);
typedef struct yetty_ycore_void_result (*yetty_yai_send_user_message_fn)(
    struct yetty_yclass_object *, struct yai_app *, const char *);
typedef struct yetty_ycore_void_result (*yetty_yai_interrupt_fn)(struct yetty_yclass_object *,
                                                                 struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_start_fn)(struct yetty_yclass_object *,
                                                             struct yai_app *);
typedef struct yetty_ycore_void_result (*yetty_yai_describe_config_fn)(struct yetty_yclass_object *,
                                                                       struct yai_app *, char *,
                                                                       size_t);
typedef struct yetty_ycore_void_result (*yetty_yai_config_knob_fn)(struct yetty_yclass_object *,
                                                                   struct yai_app *, char *,
                                                                   size_t);
typedef struct yetty_ycore_void_result (*yetty_yai_apply_config_fn)(struct yetty_yclass_object *,
                                                                    struct yai_app *, const char *,
                                                                    const char *);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_exit_fn)(struct yetty_yclass_object *,
                                                                     struct yai_app *, int64_t);
typedef struct yetty_ycore_void_result (*yetty_yai_on_child_eof_fn)(struct yetty_yclass_object *,
                                                                    struct yai_app *);
