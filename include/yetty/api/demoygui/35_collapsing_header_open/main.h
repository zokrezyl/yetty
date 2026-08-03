/* GENERATED — do not edit. */
/* Object API for regular class(es) `35_collapsing_header_open` (implementation module: demoygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_DEMOYGUI_35_COLLAPSING_HEADER_OPEN_MAIN_H
#define YETTY_YCLASSGEN_API_DEMOYGUI_35_COLLAPSING_HEADER_OPEN_MAIN_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Demo app class: a yguiapp:app subclass with no extra state. */
struct yetty_yclass_ptr_result yetty_demoygui_35_collapsing_header_open_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_demoygui_35_collapsing_header_open;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_DEMOYGUI_35_COLLAPSING_HEADER_OPEN_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_DEMOYGUI_35_COLLAPSING_HEADER_OPEN_PTR_RESULT
struct yetty_demoygui_35_collapsing_header_open_ptr_result {
    int ok;
    union {
        struct yetty_demoygui_35_collapsing_header_open *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_demoygui_35_collapsing_header_open_ptr_result
yetty_demoygui_35_collapsing_header_open_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_demoygui_35_collapsing_header_open_to(
    struct yetty_demoygui_35_collapsing_header_open *data);

struct yetty_yclass_object_ptr_result yetty_demoygui_35_collapsing_header_open_create(
    struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
