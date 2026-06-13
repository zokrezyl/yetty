/* GENERATED — do not edit. */
/* Public interface for regular class(es) `window_manager` (module: yplatform).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YPLATFORM_WINDOW_MANAGER_H
#define YETTY_YCLASSGEN_YPLATFORM_WINDOW_MANAGER_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yevent/event.h>
#include <yetty/yplatform/platform-input-pipe.h>

struct yetty_yplatform_window_manager;

struct yetty_yplatform_window_manager_ptr_result {
    int ok;
    union {
        struct yetty_yplatform_window_manager *value;
        struct yetty_ycore_error error;
    };
};

struct yetty_yclass_ptr_result yetty_yplatform_window_manager_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_yplatform_window_manager_ptr_result yetty_yplatform_window_manager_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yplatform_window_manager_to(
    struct yetty_yplatform_window_manager *data);

struct yetty_ycore_void_result yetty_yplatform_window_manager_configure(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, void *os_window,
    struct yetty_ycore_xthread_event_pipe *output_pipe,
    struct yetty_ycore_xthread_event_pipe *input_pipe);
struct yetty_ycore_void_result yetty_yplatform_window_manager_destroy(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_iconify(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_toggle_maximize(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_request_close(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_drag_by(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int dx, int dy);
struct yetty_ycore_void_result yetty_yplatform_window_manager_resize_by(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int dx, int dy, int edge);
struct yetty_ycore_void_result yetty_yplatform_window_manager_begin_interactive_move(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_begin_interactive_resize(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int edge);
struct yetty_ycore_void_result yetty_yplatform_window_manager_set_cursor(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int shape);
struct yetty_ycore_void_result yetty_yplatform_window_manager_handle_event(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    const struct yetty_yui_event *event);

typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_configure_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, void *,
    struct yetty_ycore_xthread_event_pipe *, struct yetty_ycore_xthread_event_pipe *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_destroy_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_iconify_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_toggle_maximize_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_request_close_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_drag_by_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_resize_by_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int, int, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_begin_interactive_move_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (
    *yetty_yplatform_window_manager_begin_interactive_resize_fn)(struct yetty_yclass_ctx *,
                                                                 struct yetty_yclass_object *, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_set_cursor_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_manager_handle_event_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, const struct yetty_yui_event *);

struct yetty_yclass_object_ptr_result yetty_yplatform_window_manager_create(
    struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yplatform_register(void);

#endif
