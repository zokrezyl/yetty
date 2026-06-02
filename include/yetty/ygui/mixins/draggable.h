/* GENERATED — do not edit. */
/* Public interface for mixin(es) `draggable` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_MIXINS_DRAGGABLE_H
#define YETTY_YCLASSGEN_YGUI_MIXINS_DRAGGABLE_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_draggable_mixin_get(void);

struct yetty_ygui_object;
struct draggable_data;
YETTY_YRESULT_DECLARE(yetty_ygui_draggable_data_ptr, struct draggable_data *);
struct yetty_ygui_draggable_data_ptr_result yetty_ygui_draggable_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;
typedef struct yetty_ycore_void_result (*yetty_ygui_drag_cb)(struct yetty_ygui_object *obj,
                                                             float dx, float dy, void *userdata);
struct yetty_ycore_void_result yetty_ygui_draggable_on_drag_set(struct yetty_ygui_object *obj, yetty_ygui_drag_cb cb, void *userdata);
int yetty_ygui_draggable_is_dragging(const struct yetty_ygui_object *obj);

#endif
