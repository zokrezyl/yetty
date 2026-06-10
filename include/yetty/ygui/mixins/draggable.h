/* GENERATED — do not edit. */
/* Public interface for mixin(es) `draggable` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_MIXINS_DRAGGABLE_H
#define YETTY_YCLASSGEN_YGUI_MIXINS_DRAGGABLE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_draggable_mixin_get(void);

struct yetty_ygui_object;
struct draggable_data;
YETTY_YRESULT_DECLARE(yetty_ygui_draggable_data_ptr, struct draggable_data *);
struct yetty_ygui_draggable_data_ptr_result yetty_ygui_draggable_data(
    struct yetty_ygui_object *obj);

struct yetty_ycore_int_result;

struct yetty_ycore_int_result yetty_ygui_widget_on_motion(struct yetty_yclass_ctx *yclass_ctx,
                                                          struct yetty_yclass_object *yclass_obj,
                                                          float x, float y);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_object;

struct yetty_ygui_object;
typedef struct yetty_ycore_void_result (*yetty_ygui_drag_cb)(struct yetty_ygui_object *obj,
                                                             float dx, float dy, void *userdata);
struct yetty_ycore_void_result yetty_ygui_draggable_on_drag_set(struct yetty_ygui_object *obj,
                                                                yetty_ygui_drag_cb cb,
                                                                void *userdata);
struct yetty_ycore_int_result yetty_ygui_draggable_is_dragging(const struct yetty_ygui_object *obj);

#endif
