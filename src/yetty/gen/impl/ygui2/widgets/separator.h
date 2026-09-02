/* GENERATED — do not edit. */
/* Public interface for regular class(es) `separator` (module: ygui2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI2_WIDGETS_SEPARATOR_H
#define YETTY_YCLASSGEN_YGUI2_WIDGETS_SEPARATOR_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ygui2_separator_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui2_separator;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_SEPARATOR_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_SEPARATOR_PTR_RESULT
struct yetty_ygui2_separator_ptr_result {
    int ok;
    union {
        struct yetty_ygui2_separator *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui2_separator_ptr_result yetty_ygui2_separator_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_separator_to(struct yetty_ygui2_separator *data);

struct yetty_yclass_object_ptr_result yetty_ygui2_separator_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui2_register(void);

struct yetty_ycore_void_result yetty_ygui2_separator_set_color(struct yetty_yclass_object *obj,
                                                               uint32_t packed_rgba);

#ifdef __cplusplus
}
#endif

#endif
