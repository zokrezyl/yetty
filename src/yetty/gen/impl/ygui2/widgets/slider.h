/* GENERATED — do not edit. */
/* Public interface for regular class(es) `slider` (module: ygui2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI2_WIDGETS_SLIDER_H
#define YETTY_YCLASSGEN_YGUI2_WIDGETS_SLIDER_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*yetty_ygui2_click_cb)(struct yetty_yclass_object *, void *);

struct yetty_yclass_ptr_result yetty_ygui2_slider_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui2_slider;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_SLIDER_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_SLIDER_PTR_RESULT
struct yetty_ygui2_slider_ptr_result {
    int ok;
    union {
        struct yetty_ygui2_slider *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui2_slider_ptr_result yetty_ygui2_slider_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_slider_to(struct yetty_ygui2_slider *data);

struct yetty_yclass_object_ptr_result yetty_ygui2_slider_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui2_register(void);

struct yetty_ycore_void_result yetty_ygui2_slider_set_range(struct yetty_yclass_object *obj,
                                                            float minimum, float maximum);
struct yetty_ycore_void_result yetty_ygui2_slider_set_value(struct yetty_yclass_object *obj,
                                                            float value);
struct yetty_ycore_float_result yetty_ygui2_slider_value(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_slider_on_change_set(struct yetty_yclass_object *obj,
                                                                yetty_ygui2_click_cb callback,
                                                                void *userdata);

#ifdef __cplusplus
}
#endif

#endif
