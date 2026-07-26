/* GENERATED — do not edit. */
/* Object API for regular class(es) `app` (implementation module: ybrowser).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YBROWSER_BROWSER_UI_H
#define YETTY_YCLASSGEN_API_YBROWSER_BROWSER_UI_H

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
struct yetty_ybrowser_app;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YBROWSER_APP_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YBROWSER_APP_PTR_RESULT
struct yetty_ybrowser_app_ptr_result {
    int ok;
    union {
        struct yetty_ybrowser_app *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ybrowser_app_ptr_result yetty_ybrowser_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ybrowser_app_to(struct yetty_ybrowser_app *data);

struct yetty_yclass_object_ptr_result yetty_ybrowser_app_create(struct yetty_yclass_ctx *ctx);



#ifdef __cplusplus
}
#endif

#endif
