/*
 * ygui-clickable.h — mixin: press → release → fire callback.
 *
 * Adds a "pressed" state machine to a regular widget class. Widgets that
 * want click semantics list this mixin in their YETTY_YGUI_DEFINE_CLASS
 * invocation; each instance carries a per-instance clickable_data slice.
 */
#ifndef YETTY_YGUI_MIXINS_CLICKABLE_H
#define YETTY_YGUI_MIXINS_CLICKABLE_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_clickable_mixin_get(void);

typedef struct yetty_ycore_void_result (*yetty_ygui_click_cb)(struct yetty_ygui_object *obj,
                                                              void *userdata);

/* Install the click callback on this object's clickable mixin slice.
 * Passing cb=NULL clears it. Asserts (debug) that the object's class
 * lists the clickable mixin. */
struct yetty_ycore_void_result yetty_ygui_clickable_on_click_set(struct yetty_ygui_object *obj,
                                                                 yetty_ygui_click_cb cb,
                                                                 void *userdata);

/* Read-only: is this widget currently in the "pressed" state? */
int yetty_ygui_clickable_is_pressed(const struct yetty_ygui_object *obj);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_MIXINS_CLICKABLE_H */
