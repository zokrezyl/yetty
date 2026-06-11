/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ybrowser` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YBROWSER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YBROWSER_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_ybrowser_class_get(void);

struct yetty_ygui_object;
struct yetty_ygui_ybrowser;
YETTY_YRESULT_DECLARE(yetty_ygui_ybrowser_data_ptr, struct yetty_ygui_ybrowser *);
struct yetty_ygui_ybrowser_data_ptr_result yetty_ygui_ybrowser_data(struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_ybrowser_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_ybrowser_set_html(struct yetty_ygui_object *obj,
                                                            const char *html, size_t len);

#endif
