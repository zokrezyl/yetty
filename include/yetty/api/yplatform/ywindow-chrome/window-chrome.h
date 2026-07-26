/* GENERATED — do not edit. */
/* Object API for regular class(es) `window_chrome` (implementation module: yplatform).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YPLATFORM_YWINDOW_CHROME_WINDOW_CHROME_H
#define YETTY_YCLASSGEN_API_YPLATFORM_YWINDOW_CHROME_WINDOW_CHROME_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_xthread_event_pipe;
struct yetty_yui_event;



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yplatform_window_chrome;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YPLATFORM_WINDOW_CHROME_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YPLATFORM_WINDOW_CHROME_PTR_RESULT
struct yetty_yplatform_window_chrome_ptr_result {
    int ok;
    union {
        struct yetty_yplatform_window_chrome *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yplatform_window_chrome_ptr_result yetty_yplatform_window_chrome_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yplatform_window_chrome_to(struct yetty_yplatform_window_chrome *data);

struct yetty_ycore_void_result yetty_yplatform_window_chrome_destroy(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_handle_event(struct yetty_yclass_object * obj, const struct yetty_yui_event * event);
/* Bind the chrome to its render→main bus. The platform subclass binds the native
 * window + response pipe separately (e.g. glfw_window_chrome_attach), so each
 * class writes only its own data slice. output_pipe is borrowed. */
struct yetty_ycore_void_result yetty_yplatform_window_chrome_configure(struct yetty_yclass_object * obj, struct yetty_ycore_xthread_event_pipe * output_pipe);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_iconify(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_toggle_maximize(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_request_close(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_drag_by(struct yetty_yclass_object * obj, int dx, int dy);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_resize_by(struct yetty_yclass_object * obj, int dx, int dy, int edge);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_begin_interactive_move(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_begin_interactive_resize(struct yetty_yclass_object * obj, int edge);
struct yetty_ycore_void_result yetty_yplatform_window_chrome_set_cursor(struct yetty_yclass_object * obj, int shape);

struct yetty_yclass_object_ptr_result yetty_yplatform_window_chrome_create(struct yetty_yclass_ctx *ctx);



#ifdef __cplusplus
}
#endif

#endif
