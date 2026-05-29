/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YGUI_METHODS_H
#define YETTY_YCLASSGEN_YGUI_METHODS_H

#include <yclass/class.h>
#include <yetty/ycore/types.h>
#include "yetty/ygui/types.h"

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ygui_emit_ctx;

struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_ctx * yclass_ctx, struct yetty_yclass_object * yclass_obj, float x, float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_ctx * yclass_ctx, struct yetty_yclass_object * yclass_obj, float x, float y, int button);
struct yetty_ycore_void_result yetty_ygui_widget_emit_body(struct yetty_yclass_ctx * yclass_ctx, struct yetty_yclass_object * yclass_obj, struct yetty_ygui_emit_ctx * ctx);
struct yetty_ycore_void_result yetty_ygui_constructor(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_ygui_destructor(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, float x, float y);
struct yetty_ycore_void_result yetty_ygui_widget_paint(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ygui_emit_ctx * emit_ctx);
struct yetty_ycore_void_result yetty_ygui_widget_emit_container(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, struct yetty_ygui_emit_ctx * emit_ctx);

#endif
