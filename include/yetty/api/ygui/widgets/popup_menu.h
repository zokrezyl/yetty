/* GENERATED — do not edit. */
/* Object API for regular class(es) `popup_menu` (implementation module: ygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI_WIDGETS_POPUP_MENU_H
#define YETTY_YCLASSGEN_API_YGUI_WIDGETS_POPUP_MENU_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yetty_ycore_void_result (*yetty_ygui_menu_item_cb)(struct yetty_yclass_object *, int,
                                                                  void *);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_popup_menu;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_POPUP_MENU_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_POPUP_MENU_PTR_RESULT
struct yetty_ygui_popup_menu_ptr_result {
    int ok;
    union {
        struct yetty_ygui_popup_menu *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_popup_menu_ptr_result yetty_ygui_popup_menu_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_popup_menu_to(struct yetty_ygui_popup_menu *data);

struct yetty_yclass_object_ptr_result yetty_ygui_popup_menu_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_popup_menu_add_item(struct yetty_yclass_object *obj,
                                                              const char *label,
                                                              yetty_ygui_menu_item_cb cb,
                                                              void *userdata);
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_separator(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_popup_menu_open_at(struct yetty_yclass_object *obj,
                                                             float x, float y);
struct yetty_ycore_void_result yetty_ygui_popup_menu_close(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ygui_popup_menu_is_open(const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_popup_menu_toggle_at(struct yetty_yclass_object *obj,
                                                               float x, float y);
/* Drill-down menu support. The menu has no separate submenu objects —
 * the host rebuilds the item list in place
 * (clear → set_title → set_back → add_drill_item…), so a "drill item" is
 * just a normal item whose callback repopulates the menu. */
struct yetty_ycore_void_result yetty_ygui_popup_menu_clear(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_popup_menu_set_title(struct yetty_yclass_object *obj,
                                                               const char *title);
struct yetty_ycore_void_result yetty_ygui_popup_menu_set_back(struct yetty_yclass_object *obj,
                                                              const char *label,
                                                              yetty_ygui_menu_item_cb cb,
                                                              void *userdata);
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_drill_item(struct yetty_yclass_object *obj,
                                                                    const char *label,
                                                                    yetty_ygui_menu_item_cb cb,
                                                                    void *userdata);
struct yetty_ycore_void_result yetty_ygui_popup_menu_set_modal(struct yetty_yclass_object *obj,
                                                               int modal);

#ifdef __cplusplus
}
#endif

#endif
