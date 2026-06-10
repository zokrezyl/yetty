/* GENERATED — do not edit. */
/* Public interface for regular class(es) `tree_node` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TREE_NODE_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TREE_NODE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_tree_node_class_get(void);

struct yetty_ygui_object;
struct tn_data;
YETTY_YRESULT_DECLARE(yetty_ygui_tree_node_data_ptr, struct tn_data *);
struct yetty_ygui_tree_node_data_ptr_result yetty_ygui_tree_node_data(
    struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_tree_node_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_tree_node_set_label(struct yetty_ygui_object *obj,
                                                              const char *label);
struct yetty_ycore_void_result yetty_ygui_tree_node_set_open(struct yetty_ygui_object *obj, int o);
struct yetty_ycore_int_result yetty_ygui_tree_node_is_open(const struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_tree_node_on_toggle(struct yetty_ygui_object *obj,
                                                              yetty_ygui_click_cb cb,
                                                              void *userdata);

#endif
