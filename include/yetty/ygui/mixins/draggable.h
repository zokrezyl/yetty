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

struct yetty_ygui_draggable;

struct yetty_ygui_draggable_ptr_result {
    int ok;
    union {
        struct yetty_ygui_draggable *value;
        struct yetty_ycore_error error;
    };
};

typedef struct yetty_ycore_void_result (*yetty_ygui_drag_cb)(struct yetty_yclass_object *, float,
                                                             float, void *);

struct yetty_yclass_ptr_result yetty_ygui_draggable_mixin_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_draggable_ptr_result yetty_ygui_draggable_from(struct yetty_yclass_object *obj);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_draggable_on_drag_set(struct yetty_yclass_object *obj,
                                                                yetty_ygui_drag_cb cb,
                                                                void *userdata);
struct yetty_ycore_int_result yetty_ygui_draggable_is_dragging(
    const struct yetty_yclass_object *obj);

#endif
