/* GENERATED — do not edit. */
/* Public interface for regular class(es) `chip` (module: ygui2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI2_WIDGETS_CHIP_H
#define YETTY_YCLASSGEN_YGUI2_WIDGETS_CHIP_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*yetty_ygui2_click_cb)(struct yetty_yclass_object *, void *);

struct yetty_yclass_ptr_result yetty_ygui2_chip_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui2_chip;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_CHIP_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_CHIP_PTR_RESULT
struct yetty_ygui2_chip_ptr_result {
    int ok;
    union {
        struct yetty_ygui2_chip *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui2_chip_ptr_result yetty_ygui2_chip_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_chip_to(struct yetty_ygui2_chip *data);

struct yetty_yclass_object_ptr_result yetty_ygui2_chip_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui2_register(void);

struct yetty_ycore_void_result yetty_ygui2_chip_set_label(struct yetty_yclass_object *obj,
                                                          const char *text);
struct yetty_ycore_void_result yetty_ygui2_chip_set_selectable(struct yetty_yclass_object *obj,
                                                               int selectable);
struct yetty_ycore_void_result yetty_ygui2_chip_set_selected(struct yetty_yclass_object *obj,
                                                             int selected);
struct yetty_ycore_int_result yetty_ygui2_chip_selected(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_chip_on_toggle_set(struct yetty_yclass_object *obj,
                                                              yetty_ygui2_click_cb callback,
                                                              void *userdata);

#ifdef __cplusplus
}
#endif

#endif
