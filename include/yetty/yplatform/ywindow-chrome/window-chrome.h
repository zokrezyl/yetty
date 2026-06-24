/* GENERATED — do not edit. */
/* Public interface for regular class(es) `window_chrome` (module: yplatform).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YPLATFORM_YWINDOW_CHROME_WINDOW_CHROME_H
#define YETTY_YCLASSGEN_YPLATFORM_YWINDOW_CHROME_WINDOW_CHROME_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_xthread_event_pipe;
struct yetty_yui_event;

struct yetty_yclass_ptr_result yetty_yplatform_window_chrome_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yplatform_window_chrome;
struct yetty_yplatform_window_chrome_ptr_result {
    int ok;
    union {
        struct yetty_yplatform_window_chrome *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yplatform_window_chrome_ptr_result yetty_yplatform_window_chrome_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yplatform_window_chrome_to(
    struct yetty_yplatform_window_chrome *data);

/* clang-format on */
struct yetty_ycore_void_result yetty_yplatform_window_chrome_destroy(
    struct yetty_yclass_object *obj);
/* clang-format on */
struct yetty_ycore_void_result yetty_yplatform_window_chrome_handle_event(
    struct yetty_yclass_object *obj, const struct yetty_yui_event *event);
/* clang-format on */
struct yetty_ycore_void_result yetty_yplatform_window_chrome_configure(
    struct yetty_yclass_object *obj, struct yetty_ycore_xthread_event_pipe *output_pipe);
/* clang-format on */
struct yetty_ycore_void_result yetty_yplatform_window_chrome_iconify(
    struct yetty_yclass_object *obj);
/* clang-format on */
struct yetty_ycore_void_result yetty_yplatform_window_chrome_toggle_maximize(
    struct yetty_yclass_object *obj);
/* clang-format on */
struct yetty_ycore_void_result yetty_yplatform_window_chrome_request_close(
    struct yetty_yclass_object *obj);
/* clang-format on */
struct yetty_ycore_void_result yetty_yplatform_window_chrome_drag_by(
    struct yetty_yclass_object *obj, int dx, int dy);
/* clang-format on */
struct yetty_ycore_void_result yetty_yplatform_window_chrome_resize_by(
    struct yetty_yclass_object *obj, int dx, int dy, int edge);
/* clang-format on */
struct yetty_ycore_void_result yetty_yplatform_window_chrome_begin_interactive_move(
    struct yetty_yclass_object *obj);
/* clang-format on */
struct yetty_ycore_void_result yetty_yplatform_window_chrome_begin_interactive_resize(
    struct yetty_yclass_object *obj, int edge);
/* clang-format on */
struct yetty_ycore_void_result yetty_yplatform_window_chrome_set_cursor(
    struct yetty_yclass_object *obj, int shape);

typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_destroy_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_handle_event_fn)(
    struct yetty_yclass_object *, const struct yetty_yui_event *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_configure_fn)(
    struct yetty_yclass_object *, struct yetty_ycore_xthread_event_pipe *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_iconify_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_toggle_maximize_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_request_close_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_drag_by_fn)(
    struct yetty_yclass_object *, int, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_resize_by_fn)(
    struct yetty_yclass_object *, int, int, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_begin_interactive_move_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_begin_interactive_resize_fn)(
    struct yetty_yclass_object *, int);
typedef struct yetty_ycore_void_result (*yetty_yplatform_window_chrome_set_cursor_fn)(
    struct yetty_yclass_object *, int);

struct yetty_yclass_object_ptr_result yetty_yplatform_window_chrome_create(
    struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yplatform_register(void);

#ifdef __cplusplus
}
#endif

#endif
