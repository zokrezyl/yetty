/* GENERATED — do not edit. */
/* Public interface for mixin(es) `clickable` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI_MIXINS_CLICKABLE_H
#define YETTY_YCLASSGEN_YGUI_MIXINS_CLICKABLE_H

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
struct yetty_ygui_clickable_ptr_result {
    int ok;
    union {
        struct yetty_ygui_clickable *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_ygui_clickable_ptr_result yetty_ygui_clickable_from(struct yetty_yclass_object *obj);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_clickable_on_click_set(struct yetty_yclass_object *obj, yetty_ygui_click_cb cb, void *userdata);
struct yetty_ycore_int_result yetty_ygui_clickable_is_pressed(const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_clickable_press_pos(const struct yetty_yclass_object *obj, float *x, float *y);

#ifdef __cplusplus
}
#endif

#endif
