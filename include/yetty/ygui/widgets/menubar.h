/* GENERATED — do not edit. */
/* Public interface for regular class(es) `menubar` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_MENUBAR_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_MENUBAR_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_menubar_class_get(void);

struct yetty_ygui_object;
struct menubar_data;
YETTY_YRESULT_DECLARE(yetty_ygui_menubar_data_ptr, struct menubar_data *);
struct yetty_ygui_menubar_data_ptr_result yetty_ygui_menubar_data(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_menubar_add(struct yetty_ygui_object *bar, const char *label, struct yetty_ygui_object *menu);

#endif
