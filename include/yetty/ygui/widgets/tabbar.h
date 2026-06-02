/* GENERATED — do not edit. */
/* Public interface for regular class(es) `tabbar` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TABBAR_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TABBAR_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_tabbar_class_get(void);

struct yetty_ygui_object;
struct tabbar_data;
YETTY_YRESULT_DECLARE(yetty_ygui_tabbar_data_ptr, struct tabbar_data *);
struct yetty_ygui_tabbar_data_ptr_result yetty_ygui_tabbar_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;
typedef void (*yetty_ygui_tab_close_cb)(struct yetty_ygui_object *tabbar, int index, void *userdata);
typedef void (*yetty_ygui_tab_new_cb)(struct yetty_ygui_object *tabbar, void *userdata);
struct yetty_ygui_object_ptr_result yetty_ygui_tabbar_add_tab(struct yetty_ygui_object *tabbar, const char *label);
struct yetty_ycore_void_result yetty_ygui_tabbar_remove_tab(struct yetty_ygui_object *tabbar, int index);
struct yetty_ycore_void_result yetty_ygui_tabbar_set_label(struct yetty_ygui_object *tabbar, int index, const char *label);
int yetty_ygui_tabbar_count(const struct yetty_ygui_object *tabbar);
int yetty_ygui_tabbar_active(const struct yetty_ygui_object *tabbar);
struct yetty_ycore_void_result yetty_ygui_tabbar_set_active(struct yetty_ygui_object *tabbar, int index);
struct yetty_ycore_void_result yetty_ygui_tabbar_set_on_close(struct yetty_ygui_object *tabbar, yetty_ygui_tab_close_cb cb, void *userdata);
struct yetty_ycore_void_result yetty_ygui_tabbar_set_on_new_tab(struct yetty_ygui_object *tabbar, yetty_ygui_tab_new_cb cb, void *userdata);

#endif
