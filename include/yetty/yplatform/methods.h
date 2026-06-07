/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YPLATFORM_METHODS_H
#define YETTY_YCLASSGEN_YPLATFORM_METHODS_H

#include <yetty/yclass/class.h>
#include <yetty/ycore/types.h>
#include "yetty/yplatform/types.h"

struct yetty_ycore_void_result;
struct yetty_ycore_xthread_event_pipe;
struct yetty_yui_event;

struct yetty_ycore_void_result yetty_yplatform_window_manager_configure(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, void * os_window, struct yetty_ycore_xthread_event_pipe * output_pipe, struct yetty_ycore_xthread_event_pipe * input_pipe);
struct yetty_ycore_void_result yetty_yplatform_window_manager_destroy(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_iconify(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_toggle_maximize(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_request_close(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_drag_by(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, int dx, int dy);
struct yetty_ycore_void_result yetty_yplatform_window_manager_resize_by(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, int dx, int dy, int edge);
struct yetty_ycore_void_result yetty_yplatform_window_manager_begin_interactive_move(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yplatform_window_manager_begin_interactive_resize(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, int edge);
struct yetty_ycore_void_result yetty_yplatform_window_manager_set_cursor(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, int shape);
struct yetty_ycore_void_result yetty_yplatform_window_manager_handle_event(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, const struct yetty_yui_event * event);

#endif
