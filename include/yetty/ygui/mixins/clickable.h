/* GENERATED — do not edit. */
/* Public interface for mixin(es) `clickable` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_MIXINS_CLICKABLE_H
#define YETTY_YCLASSGEN_YGUI_MIXINS_CLICKABLE_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_clickable_mixin_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
typedef struct yetty_ycore_void_result (*yetty_ygui_click_cb)(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              void *userdata);

/* Install the click callback on this object's clickable mixin slice.
 * Passing cb=NULL clears it. Asserts (debug) that the object's class
 * lists the clickable mixin. */
struct yetty_ycore_void_result yetty_ygui_clickable_on_click_set(struct yetty_ygui_object *obj,
                                                                 yetty_ygui_click_cb cb,
                                                                 void *userdata);

/* Read-only: is this widget currently in the "pressed" state? */
int yetty_ygui_clickable_is_pressed(const struct yetty_ygui_object *obj);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
