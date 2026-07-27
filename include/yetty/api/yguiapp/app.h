/* GENERATED — do not edit. */
/* Object API for regular class(es) `app` (implementation module: yguiapp).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUIAPP_APP_H
#define YETTY_YCLASSGEN_API_YGUIAPP_APP_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yguiapp_app;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUIAPP_APP_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUIAPP_APP_PTR_RESULT
struct yetty_yguiapp_app_ptr_result {
    int ok;
    union {
        struct yetty_yguiapp_app *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yguiapp_app_ptr_result yetty_yguiapp_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yguiapp_app_to(struct yetty_yguiapp_app *data);
struct yetty_yclass_object_ptr_result yetty_yguiapp_app_root_get(struct yetty_yclass_object *obj);

/*===========================================================================
 * build — the widget-tree population hook. A ygui app subclasses yguiapp:app
 * and overrides this to add widgets under `root`. Local-only: `root` is a
 * live in-process widget object, never wire-marshalled. The default is empty.
 *=========================================================================*/
struct yetty_ycore_void_result yetty_yguiapp_build(struct yetty_yclass_object *app,
                                                   struct yetty_yclass_object *root);

struct yetty_yclass_object_ptr_result yetty_yguiapp_app_create(struct yetty_yclass_ctx *ctx);

/* Stop the app's event loop — the clean-quit path for app subclasses that
 * install their own key handler (e.g. an Esc-to-quit demo) and so bypass the
 * default 'q'/Ctrl-C quit above. A no-op when no loop is present (headless). */
struct yetty_ycore_void_result yetty_yguiapp_app_quit(struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
