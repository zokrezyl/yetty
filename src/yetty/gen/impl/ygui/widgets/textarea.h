/* GENERATED — do not edit. */
/* Public interface for regular class(es) `textarea` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TEXTAREA_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TEXTAREA_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ygui_textarea_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_textarea;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_TEXTAREA_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_TEXTAREA_PTR_RESULT
struct yetty_ygui_textarea_ptr_result {
    int ok;
    union {
        struct yetty_ygui_textarea *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_textarea_ptr_result yetty_ygui_textarea_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_textarea_to(struct yetty_ygui_textarea *data);

struct yetty_yclass_object_ptr_result yetty_ygui_textarea_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_textarea_set_text(struct yetty_yclass_object *obj,
                                                            const char *text);
struct yetty_ycore_const_char_ptr_result yetty_ygui_textarea_get_text(
    const struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
