/*
 * ygui-draggable.h — mixin: press + motion → drag, release → finalize.
 *
 * Tracks the press point and emits a "delta" via the drag callback on
 * every motion while pressed. Widgets like scrollbars and resizable
 * dividers list this mixin to get drag semantics for free.
 */
#ifndef YETTY_YGUI_MIXINS_DRAGGABLE_H
#define YETTY_YGUI_MIXINS_DRAGGABLE_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_draggable_mixin_get(void);

typedef struct yetty_ycore_void_result (*yetty_ygui_drag_cb)(struct yetty_ygui_object *obj,
                                                             float dx, float dy, void *userdata);

struct yetty_ycore_void_result yetty_ygui_draggable_on_drag_set(struct yetty_ygui_object *obj,
                                                                yetty_ygui_drag_cb cb,
                                                                void *userdata);

int yetty_ygui_draggable_is_dragging(const struct yetty_ygui_object *obj);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_MIXINS_DRAGGABLE_H */
