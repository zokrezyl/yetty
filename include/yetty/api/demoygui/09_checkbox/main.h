/* GENERATED — do not edit. */
/* Object API for regular class(es) `09_checkbox` (implementation module: demoygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_DEMOYGUI_09_CHECKBOX_MAIN_H
#define YETTY_YCLASSGEN_API_DEMOYGUI_09_CHECKBOX_MAIN_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Demo app class: a yguiapp:app subclass with no extra state. */
struct yetty_yclass_ptr_result yetty_demoygui_09_checkbox_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_demoygui_09_checkbox;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_DEMOYGUI_09_CHECKBOX_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_DEMOYGUI_09_CHECKBOX_PTR_RESULT
struct yetty_demoygui_09_checkbox_ptr_result {
    int ok;
    union {
        struct yetty_demoygui_09_checkbox *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_demoygui_09_checkbox_ptr_result yetty_demoygui_09_checkbox_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_demoygui_09_checkbox_to(
    struct yetty_demoygui_09_checkbox *data);

struct yetty_yclass_object_ptr_result yetty_demoygui_09_checkbox_create(
    struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
