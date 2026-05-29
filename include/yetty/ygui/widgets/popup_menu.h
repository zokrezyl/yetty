/* GENERATED — do not edit. */
/* Public interface for regular class(es) `popup_menu` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_POPUP_MENU_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_POPUP_MENU_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_popup_menu_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
typedef struct yetty_ycore_void_result (*yetty_ygui_menu_item_cb)(struct yetty_yclass_ctx *ctx,
                                                                  struct yetty_yclass_object *menu,
                                                                  int item_index, void *userdata);

/* Add a clickable item. `label` is copied. Returns the item index. */
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_item(struct yetty_ygui_object *obj,
                                                              const char *label,
                                                              yetty_ygui_menu_item_cb cb,
                                                              void *userdata);

/* Add a separator row (1px hairline). */
struct yetty_ycore_void_result yetty_ygui_popup_menu_add_separator(struct yetty_ygui_object *obj);

/* Open the menu at viewport coords (x, y). Resizes the menu to fit
 * the current item set + sets layout absolute pos. */
struct yetty_ycore_void_result yetty_ygui_popup_menu_open_at(struct yetty_ygui_object *obj, float x,
                                                             float y);

struct yetty_ycore_void_result yetty_ygui_popup_menu_close(struct yetty_ygui_object *obj);

int yetty_ygui_popup_menu_is_open(const struct yetty_ygui_object *obj);

/* Convenience: returns 1 if the menu is currently open. Caller passes
 * the toggle source's (x, y). Switches open ↔ close. */
struct yetty_ycore_void_result yetty_ygui_popup_menu_toggle_at(struct yetty_ygui_object *obj,
                                                               float x, float y);

/* Drill-down menu support (yui rebuilds the item list in place). */
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
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
