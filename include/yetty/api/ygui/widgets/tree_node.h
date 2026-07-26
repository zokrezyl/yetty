/* GENERATED — do not edit. */
/* Object API for regular class(es) `tree_node` (implementation module: ygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI_WIDGETS_TREE_NODE_H
#define YETTY_YCLASSGEN_API_YGUI_WIDGETS_TREE_NODE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct yetty_ycore_void_result (*yetty_ygui_click_cb)(struct yetty_yclass_object *, void *);



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_tree_node;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_TREE_NODE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_TREE_NODE_PTR_RESULT
struct yetty_ygui_tree_node_ptr_result {
    int ok;
    union {
        struct yetty_ygui_tree_node *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_tree_node_ptr_result yetty_ygui_tree_node_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_tree_node_to(struct yetty_ygui_tree_node *data);

struct yetty_yclass_object_ptr_result yetty_ygui_tree_node_create(struct yetty_yclass_ctx *ctx);



struct yetty_ycore_void_result yetty_ygui_tree_node_set_label(struct yetty_yclass_object *obj, const char *label);
struct yetty_ycore_void_result yetty_ygui_tree_node_set_open(struct yetty_yclass_object *obj, int o);
struct yetty_ycore_int_result yetty_ygui_tree_node_is_open(const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_tree_node_on_toggle(struct yetty_yclass_object *obj, yetty_ygui_click_cb cb, void *userdata);

#ifdef __cplusplus
}
#endif

#endif
