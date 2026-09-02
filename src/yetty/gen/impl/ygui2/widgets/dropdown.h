/* GENERATED — do not edit. */
/* Public interface for regular class(es) `dropdown` (module: ygui2).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI2_WIDGETS_DROPDOWN_H
#define YETTY_YCLASSGEN_YGUI2_WIDGETS_DROPDOWN_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*yetty_ygui2_select_cb)(struct yetty_yclass_object *, uint32_t, void *);

struct yetty_yclass_ptr_result yetty_ygui2_dropdown_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui2_dropdown;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_DROPDOWN_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_DROPDOWN_PTR_RESULT
struct yetty_ygui2_dropdown_ptr_result {
    int ok;
    union {
        struct yetty_ygui2_dropdown *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui2_dropdown_ptr_result yetty_ygui2_dropdown_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_dropdown_to(struct yetty_ygui2_dropdown *data);

struct yetty_yclass_object_ptr_result yetty_ygui2_dropdown_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui2_register(void);

struct yetty_ycore_void_result yetty_ygui2_dropdown_item_add(struct yetty_yclass_object *obj,
                                                             const char *text);
struct yetty_ycore_void_result yetty_ygui2_dropdown_set_selected(struct yetty_yclass_object *obj,
                                                                 int selected_index);
struct yetty_ycore_int_result yetty_ygui2_dropdown_selected(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_dropdown_on_change_set(struct yetty_yclass_object *obj,
                                                                  yetty_ygui2_select_cb callback,
                                                                  void *userdata);

#ifdef __cplusplus
}
#endif

#endif
