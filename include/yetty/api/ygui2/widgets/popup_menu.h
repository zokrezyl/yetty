/* GENERATED — do not edit. */
/* Object API for regular class(es) `popup_menu` (implementation module: ygui2).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI2_WIDGETS_POPUP_MENU_H
#define YETTY_YCLASSGEN_API_YGUI2_WIDGETS_POPUP_MENU_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*yetty_ygui2_select_cb)(struct yetty_yclass_object *, uint32_t, void *);

struct yetty_yclass_ptr_result yetty_ygui2_popup_menu_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui2_popup_menu;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_POPUP_MENU_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_POPUP_MENU_PTR_RESULT
struct yetty_ygui2_popup_menu_ptr_result {
    int ok;
    union {
        struct yetty_ygui2_popup_menu *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui2_popup_menu_ptr_result yetty_ygui2_popup_menu_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_popup_menu_to(
    struct yetty_ygui2_popup_menu *data);

struct yetty_yclass_object_ptr_result yetty_ygui2_popup_menu_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui2_popup_menu_item_add(struct yetty_yclass_object *obj,
                                                               const char *text);
struct yetty_ycore_void_result yetty_ygui2_popup_menu_items_clear(struct yetty_yclass_object *obj);
/* Content height for the current item count — the app sizes the popup with
 * this before showing it. */
struct yetty_ycore_float_result yetty_ygui2_popup_menu_content_height(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_popup_menu_on_select_set(struct yetty_yclass_object *obj,
                                                                    yetty_ygui2_select_cb callback,
                                                                    void *userdata);

#ifdef __cplusplus
}
#endif

#endif
