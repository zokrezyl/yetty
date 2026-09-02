/* GENERATED — do not edit. */
/* Public interface for regular class(es) `stepper` (module: ygui2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI2_WIDGETS_STEPPER_H
#define YETTY_YCLASSGEN_YGUI2_WIDGETS_STEPPER_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ygui2_stepper_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui2_stepper;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_STEPPER_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_STEPPER_PTR_RESULT
struct yetty_ygui2_stepper_ptr_result {
    int ok;
    union {
        struct yetty_ygui2_stepper *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui2_stepper_ptr_result yetty_ygui2_stepper_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_stepper_to(struct yetty_ygui2_stepper *data);

struct yetty_yclass_object_ptr_result yetty_ygui2_stepper_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui2_register(void);

struct yetty_ycore_void_result yetty_ygui2_stepper_set_count(struct yetty_yclass_object *obj,
                                                             uint32_t step_count);
struct yetty_ycore_void_result yetty_ygui2_stepper_set_current(struct yetty_yclass_object *obj,
                                                               uint32_t current);
struct yetty_ycore_int_result yetty_ygui2_stepper_current(struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
