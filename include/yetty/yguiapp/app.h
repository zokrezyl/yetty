/* GENERATED — do not edit. */
/* Public interface for regular class(es) `app` (module: yguiapp).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUIAPP_APP_H
#define YETTY_YCLASSGEN_YGUIAPP_APP_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*===========================================================================
 * Class data slice. The host owns the whole environment; the per-instance
 * pointers below are created in run() and torn down at the end of run().
 *=========================================================================*/
struct yetty_yclass_ptr_result yetty_yguiapp_app_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yguiapp_app;
struct yetty_yguiapp_app_ptr_result {
    int ok;
    union {
        struct yetty_yguiapp_app *value;
        struct yetty_ycore_error error;
    };
};
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

typedef struct yetty_ycore_void_result (*yetty_yguiapp_build_fn)(struct yetty_yclass_object *,
                                                                 struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_yguiapp_app_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yguiapp_register(void);

/* Stop the app's event loop — the clean-quit path for app subclasses that
 * install their own key handler (e.g. an Esc-to-quit demo) and so bypass the
 * default 'q'/Ctrl-C quit above. A no-op when no loop is present (headless). */
struct yetty_ycore_void_result yetty_yguiapp_app_quit(struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
