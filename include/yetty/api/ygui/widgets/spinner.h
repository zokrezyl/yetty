/* GENERATED — do not edit. */
/* Object API for regular class(es) `spinner` (implementation module: ygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI_WIDGETS_SPINNER_H
#define YETTY_YCLASSGEN_API_YGUI_WIDGETS_SPINNER_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_spinner;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_SPINNER_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_SPINNER_PTR_RESULT
struct yetty_ygui_spinner_ptr_result {
    int ok;
    union {
        struct yetty_ygui_spinner *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_spinner_ptr_result yetty_ygui_spinner_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_spinner_to(struct yetty_ygui_spinner *data);

struct yetty_yclass_object_ptr_result yetty_ygui_spinner_create(struct yetty_yclass_ctx *ctx);



struct yetty_ycore_void_result yetty_ygui_spinner_set_value(struct yetty_yclass_object *obj, float v);
struct yetty_ycore_void_result yetty_ygui_spinner_set_range(struct yetty_yclass_object *obj, float mn, float mx, float step);
struct yetty_ycore_float_result yetty_ygui_spinner_get_value(const struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
