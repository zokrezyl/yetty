/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ybrowser` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YBROWSER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YBROWSER_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_ybrowser_class_get(void);

struct yetty_ygui_object;
struct ybrowser_data;
YETTY_YRESULT_DECLARE(yetty_ygui_ybrowser_data_ptr, struct ybrowser_data *);
struct yetty_ygui_ybrowser_data_ptr_result yetty_ygui_ybrowser_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_ybrowser_set_html(struct yetty_ygui_object *obj,
                                                            const char *html, size_t len);

#endif
