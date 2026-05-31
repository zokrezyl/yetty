/* GENERATED — do not edit. */
/* Public interface for mixin(es) `draggable` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_MIXINS_DRAGGABLE_H
#define YETTY_YCLASSGEN_YGUI_MIXINS_DRAGGABLE_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_draggable_mixin_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
struct yetty_ygui_object;

typedef struct yetty_ycore_void_result (*yetty_ygui_drag_cb)(struct yetty_ygui_object *obj,
                                                             float dx, float dy, void *userdata);

struct yetty_ycore_void_result yetty_ygui_draggable_on_drag_set(struct yetty_ygui_object *obj,
                                                                yetty_ygui_drag_cb cb,
                                                                void *userdata);

int yetty_ygui_draggable_is_dragging(const struct yetty_ygui_object *obj);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
