/* GENERATED — do not edit. */
/* Object API for regular class(es) `radio` (implementation module: ygui2).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI2_WIDGETS_RADIO_H
#define YETTY_YCLASSGEN_API_YGUI2_WIDGETS_RADIO_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*yetty_ygui2_click_cb)(struct yetty_yclass_object *, void *);

struct yetty_yclass_ptr_result yetty_ygui2_radio_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui2_radio;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_RADIO_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI2_RADIO_PTR_RESULT
struct yetty_ygui2_radio_ptr_result {
    int ok;
    union {
        struct yetty_ygui2_radio *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui2_radio_ptr_result yetty_ygui2_radio_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui2_radio_to(struct yetty_ygui2_radio *data);

struct yetty_yclass_object_ptr_result yetty_ygui2_radio_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui2_radio_set_label(struct yetty_yclass_object *obj,
                                                           const char *text);
struct yetty_ycore_void_result yetty_ygui2_radio_set_selected(struct yetty_yclass_object *obj,
                                                              int selected);
struct yetty_ycore_int_result yetty_ygui2_radio_selected(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui2_radio_on_select_set(struct yetty_yclass_object *obj,
                                                               yetty_ygui2_click_cb callback,
                                                               void *userdata);

#ifdef __cplusplus
}
#endif

#endif
