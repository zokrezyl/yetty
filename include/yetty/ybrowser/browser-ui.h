/* GENERATED — do not edit. */
/* Public interface for regular class(es) `app` (module: ybrowser).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YBROWSER_BROWSER_UI_H
#define YETTY_YCLASSGEN_YBROWSER_BROWSER_UI_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Standalone browser UI as a yclass class `ybrowser:app` (subclass of yapp:app).
 * The whole block is gated by YETTY_YBROWSER_HAS_STANDALONE (only the
 * standalone, GPU-windowed build): in client / one-shot modes ybrowser has no
 * window. codegen sees this class because the Makefile passes the guard macro
 * via YCLASS_DEFINES; the generated browser-ui.gen.c is #included at the foot,
 * inside the same guard, so reduced builds never compile it.
 */
struct yetty_yclass_ptr_result yetty_ybrowser_app_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ybrowser_app;
struct yetty_ybrowser_app_ptr_result {
    int ok;
    union {
        struct yetty_ybrowser_app *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_ybrowser_app_ptr_result yetty_ybrowser_app_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ybrowser_app_to(struct yetty_ybrowser_app *data);

struct yetty_yclass_object_ptr_result yetty_ybrowser_app_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ybrowser_register(void);

#ifdef __cplusplus
}
#endif

#endif
