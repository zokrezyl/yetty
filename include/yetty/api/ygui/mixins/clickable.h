/* GENERATED — do not edit. */
/* Object API for mixin(es) `clickable` (implementation module: ygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI_MIXINS_CLICKABLE_H
#define YETTY_YCLASSGEN_API_YGUI_MIXINS_CLICKABLE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yetty_ycore_void_result (*yetty_ygui_click_cb)(struct yetty_yclass_object *, void *);

struct yetty_yclass_ptr_result yetty_ygui_clickable_mixin_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_clickable;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_CLICKABLE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_CLICKABLE_PTR_RESULT
struct yetty_ygui_clickable_ptr_result {
    int ok;
    union {
        struct yetty_ygui_clickable *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_clickable_ptr_result yetty_ygui_clickable_from(struct yetty_yclass_object *obj);

struct yetty_ycore_void_result yetty_ygui_clickable_on_click_set(struct yetty_yclass_object *obj,
                                                                 yetty_ygui_click_cb cb,
                                                                 void *userdata);
struct yetty_ycore_int_result yetty_ygui_clickable_is_pressed(
    const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_clickable_press_pos(const struct yetty_yclass_object *obj,
                                                              float *x, float *y);

#ifdef __cplusplus
}
#endif

#endif
