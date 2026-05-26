/* ygui-tree_node.h — collapsible labelled node. Children added with
 * yetty_ygui_add are nested under the node and indented when open.
 * Click on the row toggles open/closed. */
#ifndef YETTY_YGUI_WIDGETS_TREE_NODE_H
#define YETTY_YGUI_WIDGETS_TREE_NODE_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_tree_node_class_get(void);
struct yetty_ycore_void_result yetty_ygui_tree_node_set_label(struct yetty_ygui_object *obj,
                                                              const char *label);
struct yetty_ycore_void_result yetty_ygui_tree_node_set_open(struct yetty_ygui_object *obj,
                                                             int open);
int yetty_ygui_tree_node_is_open(const struct yetty_ygui_object *obj);
#ifdef __cplusplus
}
#endif
#endif
