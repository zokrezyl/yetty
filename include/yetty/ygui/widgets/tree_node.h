/* GENERATED — do not edit. */
/* Public interface for regular class(es) `tree_node` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TREE_NODE_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TREE_NODE_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>
#include <yetty/ygui/mixins/clickable.h>

struct yetty_yclass_ptr_result yetty_ygui_tree_node_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
struct yetty_ycore_void_result yetty_ygui_tree_node_set_label(struct yetty_ygui_object *obj,
                                                              const char *label);
struct yetty_ycore_void_result yetty_ygui_tree_node_set_open(struct yetty_ygui_object *obj,
                                                             int open);
int yetty_ygui_tree_node_is_open(const struct yetty_ygui_object *obj);
/* Fired after the header toggles open/closed (read state via is_open). */
struct yetty_ycore_void_result yetty_ygui_tree_node_on_toggle(struct yetty_ygui_object *obj,
                                                              yetty_ygui_click_cb cb,
                                                              void *userdata);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
