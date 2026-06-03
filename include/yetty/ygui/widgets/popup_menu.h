/* GENERATED — do not edit. */
/* Public interface for regular class(es) `popup_menu` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_POPUP_MENU_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_POPUP_MENU_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_popup_menu_class_get(void);

struct yetty_ygui_object;
struct popup_menu_data;
YETTY_YRESULT_DECLARE(yetty_ygui_popup_menu_data_ptr, struct popup_menu_data *);
struct yetty_ygui_popup_menu_data_ptr_result yetty_ygui_popup_menu_data(struct yetty_ygui_object *obj);

typedef struct yetty_ycore_void_result (*yetty_ygui_menu_item_cb)(struct yetty_yclass_ctx *ctx,
                                                                  struct yetty_yclass_object *menu,
                                                                  int item_index, void *userdata);
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_item(struct yetty_ygui_object *obj, const char *label, yetty_ygui_menu_item_cb cb, void *userdata);
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_separator(struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_popup_menu_open_at(struct yetty_ygui_object *obj, float x, float y);
struct yetty_ycore_void_result yetty_ygui_popup_menu_close(struct yetty_ygui_object *obj);
int yetty_ygui_popup_menu_is_open(const struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_popup_menu_toggle_at(struct yetty_ygui_object *obj, float x, float y);
struct yetty_ycore_void_result yetty_ygui_popup_menu_clear(struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_popup_menu_set_title(struct yetty_ygui_object *obj, const char *title);
struct yetty_ycore_void_result yetty_ygui_popup_menu_set_back(struct yetty_ygui_object *obj, const char *label, yetty_ygui_menu_item_cb cb, void *userdata);
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_drill_item(struct yetty_ygui_object *obj, const char *label, yetty_ygui_menu_item_cb cb, void *userdata);
struct yetty_ycore_void_result yetty_ygui_popup_menu_set_modal(struct yetty_ygui_object *obj, int modal);

#endif
