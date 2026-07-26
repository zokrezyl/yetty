/* GENERATED — do not edit. */
/* Object API for regular class(es) `dialog` (implementation module: ygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI_WIDGETS_DIALOG_H
#define YETTY_YCLASSGEN_API_YGUI_WIDGETS_DIALOG_H

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
struct yetty_ygui_dialog;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_DIALOG_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_DIALOG_PTR_RESULT
struct yetty_ygui_dialog_ptr_result {
    int ok;
    union {
        struct yetty_ygui_dialog *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_dialog_ptr_result yetty_ygui_dialog_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_dialog_to(struct yetty_ygui_dialog *data);

struct yetty_yclass_object_ptr_result yetty_ygui_dialog_create(struct yetty_yclass_ctx *ctx);



struct yetty_ycore_void_result yetty_ygui_dialog_set_title(struct yetty_yclass_object *obj, const char *title);
/* Enable/disable the titlebar ✕ close button. The widget only draws it;
 * the host hit-tests the top-right DIALOG_CLOSE_W square of the titlebar
 * and calls yetty_ygui_dialog_close. */
struct yetty_ycore_void_result yetty_ygui_dialog_set_closable(struct yetty_yclass_object *obj, int closable);
struct yetty_ycore_void_result yetty_ygui_dialog_open_at(struct yetty_yclass_object *obj, float x, float y, float width, float height);
struct yetty_ycore_void_result yetty_ygui_dialog_close(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ygui_dialog_is_open(const struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
