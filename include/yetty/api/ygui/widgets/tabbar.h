/* GENERATED — do not edit. */
/* Object API for regular class(es) `tabbar` (implementation module: ygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI_WIDGETS_TABBAR_H
#define YETTY_YCLASSGEN_API_YGUI_WIDGETS_TABBAR_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*yetty_ygui_tab_close_cb)(struct yetty_yclass_object *, int, void *);
typedef void (*yetty_ygui_tab_new_cb)(struct yetty_yclass_object *, void *);



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_tabbar;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_TABBAR_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_TABBAR_PTR_RESULT
struct yetty_ygui_tabbar_ptr_result {
    int ok;
    union {
        struct yetty_ygui_tabbar *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_tabbar_ptr_result yetty_ygui_tabbar_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_tabbar_to(struct yetty_ygui_tabbar *data);

struct yetty_yclass_object_ptr_result yetty_ygui_tabbar_create(struct yetty_yclass_ctx *ctx);



struct yetty_yclass_object_ptr_result yetty_ygui_tabbar_add_tab(struct yetty_yclass_object *tabbar, const char *label);
struct yetty_ycore_void_result yetty_ygui_tabbar_remove_tab(struct yetty_yclass_object *tabbar, int index);
struct yetty_ycore_void_result yetty_ygui_tabbar_set_label(struct yetty_yclass_object *tabbar, int index, const char *label);
struct yetty_ycore_int_result yetty_ygui_tabbar_count(const struct yetty_yclass_object *tabbar);
struct yetty_ycore_int_result yetty_ygui_tabbar_active(const struct yetty_yclass_object *tabbar);
struct yetty_ycore_void_result yetty_ygui_tabbar_set_active(struct yetty_yclass_object *tabbar, int index);
struct yetty_ycore_void_result yetty_ygui_tabbar_set_on_close(struct yetty_yclass_object *tabbar, yetty_ygui_tab_close_cb cb, void *userdata);
struct yetty_ycore_void_result yetty_ygui_tabbar_set_on_new_tab(struct yetty_yclass_object *tabbar, yetty_ygui_tab_new_cb cb, void *userdata);

#ifdef __cplusplus
}
#endif

#endif
