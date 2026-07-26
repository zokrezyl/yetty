/* GENERATED — do not edit. */
/* Object API for regular class(es) `panel` (implementation module: ygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI_WIDGETS_PANEL_H
#define YETTY_YCLASSGEN_API_YGUI_WIDGETS_PANEL_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ycore_rgba;



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_panel;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_PANEL_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_PANEL_PTR_RESULT
struct yetty_ygui_panel_ptr_result {
    int ok;
    union {
        struct yetty_ygui_panel *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_panel_ptr_result yetty_ygui_panel_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_panel_to(struct yetty_ygui_panel *data);

struct yetty_yclass_object_ptr_result yetty_ygui_panel_create(struct yetty_yclass_ctx *ctx);



struct yetty_ycore_void_result yetty_ygui_panel_set_bg(struct yetty_yclass_object *obj, struct yetty_ycore_rgba color);
struct yetty_ycore_void_result yetty_ygui_panel_set_border(struct yetty_yclass_object *obj, struct yetty_ycore_rgba color, float width_px);

#ifdef __cplusplus
}
#endif

#endif
