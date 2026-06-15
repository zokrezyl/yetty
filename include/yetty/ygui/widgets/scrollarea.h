/* GENERATED — do not edit. */
/* Public interface for regular class(es) `scrollarea` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_SCROLLAREA_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_SCROLLAREA_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ygui_scrollarea_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_scrollarea;
struct yetty_ygui_scrollarea_ptr_result {
    int ok;
    union {
        struct yetty_ygui_scrollarea *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_ygui_scrollarea_ptr_result yetty_ygui_scrollarea_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_scrollarea_to(struct yetty_ygui_scrollarea *data);

struct yetty_yclass_object_ptr_result yetty_ygui_scrollarea_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

#ifdef __cplusplus
}
#endif

#endif
