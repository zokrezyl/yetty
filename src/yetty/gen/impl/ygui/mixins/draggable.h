/* GENERATED — do not edit. */
/* Public interface for mixin(es) `draggable` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI_MIXINS_DRAGGABLE_H
#define YETTY_YCLASSGEN_YGUI_MIXINS_DRAGGABLE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yetty_ycore_void_result (*yetty_ygui_drag_cb)(struct yetty_yclass_object *, float, float, void *);

struct yetty_yclass_ptr_result yetty_ygui_draggable_mixin_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_draggable;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_DRAGGABLE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_DRAGGABLE_PTR_RESULT
struct yetty_ygui_draggable_ptr_result {
    int ok;
    union {
        struct yetty_ygui_draggable *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_draggable_ptr_result yetty_ygui_draggable_from(struct yetty_yclass_object *obj);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_draggable_on_drag_set(struct yetty_yclass_object *obj, yetty_ygui_drag_cb cb, void *userdata);
struct yetty_ycore_void_result yetty_ygui_draggable_press_point(struct yetty_yclass_object *obj, float *out_x, float *out_y);
struct yetty_ycore_int_result yetty_ygui_draggable_is_dragging(const struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
