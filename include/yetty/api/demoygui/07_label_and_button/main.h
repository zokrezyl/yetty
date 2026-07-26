/* GENERATED — do not edit. */
/* Object API for regular class(es) `07_label_and_button` (implementation module: demoygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_DEMOYGUI_07_LABEL_AND_BUTTON_MAIN_H
#define YETTY_YCLASSGEN_API_DEMOYGUI_07_LABEL_AND_BUTTON_MAIN_H

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
struct yetty_demoygui_07_label_and_button;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_DEMOYGUI_07_LABEL_AND_BUTTON_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_DEMOYGUI_07_LABEL_AND_BUTTON_PTR_RESULT
struct yetty_demoygui_07_label_and_button_ptr_result {
    int ok;
    union {
        struct yetty_demoygui_07_label_and_button *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_demoygui_07_label_and_button_ptr_result yetty_demoygui_07_label_and_button_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_demoygui_07_label_and_button_to(struct yetty_demoygui_07_label_and_button *data);

struct yetty_yclass_object_ptr_result yetty_demoygui_07_label_and_button_create(struct yetty_yclass_ctx *ctx);



#ifdef __cplusplus
}
#endif

#endif
