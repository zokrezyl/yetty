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

struct yetty_yclass_object;

struct yetty_ygui_ybrowser;

struct yetty_ygui_ybrowser_ptr_result {
    int ok;
    union {
        struct yetty_ygui_ybrowser *value;
        struct yetty_ycore_error error;
    };
};

struct yetty_yclass_ptr_result yetty_ygui_ybrowser_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_ybrowser_ptr_result yetty_ygui_ybrowser_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_ybrowser_to(struct yetty_ygui_ybrowser *data);

struct yetty_yclass_object_ptr_result yetty_ygui_ybrowser_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_ybrowser_set_html(struct yetty_yclass_object *obj,
                                                            const char *html, size_t len);

#endif
