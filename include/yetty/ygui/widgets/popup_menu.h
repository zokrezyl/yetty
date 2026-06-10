/* GENERATED — do not edit. */
/* Public interface for regular class(es) `popup_menu` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_POPUP_MENU_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_POPUP_MENU_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_popup_menu_class_get(void);

struct yetty_ygui_object;
struct popup_menu_data;
YETTY_YRESULT_DECLARE(yetty_ygui_popup_menu_data_ptr, struct popup_menu_data *);
struct yetty_ygui_popup_menu_data_ptr_result yetty_ygui_popup_menu_data(
    struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_popup_menu_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_object;

typedef struct yetty_ycore_void_result (*yetty_ygui_menu_item_cb)(struct yetty_yclass_ctx *ctx,
                                                                  struct yetty_yclass_object *menu,
                                                                  int item_index, void *userdata);
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_item(struct yetty_ygui_object *obj,
                                                              const char *label,
                                                              yetty_ygui_menu_item_cb cb,
                                                              void *userdata);
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_separator(struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_popup_menu_open_at(struct yetty_ygui_object *obj, float x,
                                                             float y);
struct yetty_ycore_void_result yetty_ygui_popup_menu_close(struct yetty_ygui_object *obj);
struct yetty_ycore_int_result yetty_ygui_popup_menu_is_open(const struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_popup_menu_toggle_at(struct yetty_ygui_object *obj,
                                                               float x, float y);
struct yetty_ycore_void_result yetty_ygui_popup_menu_clear(struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_popup_menu_set_title(struct yetty_ygui_object *obj,
                                                               const char *title);
struct yetty_ycore_void_result yetty_ygui_popup_menu_set_back(struct yetty_ygui_object *obj,
                                                              const char *label,
                                                              yetty_ygui_menu_item_cb cb,
                                                              void *userdata);
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_drill_item(struct yetty_ygui_object *obj,
                                                                    const char *label,
                                                                    yetty_ygui_menu_item_cb cb,
                                                                    void *userdata);
struct yetty_ycore_void_result yetty_ygui_popup_menu_set_modal(struct yetty_ygui_object *obj,
                                                               int modal);

#endif
