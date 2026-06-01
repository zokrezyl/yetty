/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ynode` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YNODE_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YNODE_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_ynode_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
#include <yetty/ycore/result.h>

struct yetty_ygui_object;

/*-----------------------------------------------------------------------------
 * ynode — a single node in a ynodes editor.
 *
 * A ynode is a vbox: its title bar, frame and pins are painted by the
 * node, and its body (everything below the title, between the pin
 * margins) lays out child widgets in a column. Add any ygui widget under
 * a node with yetty_ygui_add(<class>, node) and it becomes node content.
 *
 * A ynode must be a child of a ynodes editor — the editor maps the node's
 * graph-space position to the screen via its pan/zoom view.
 *---------------------------------------------------------------------------*/

/* Title shown in the node's header bar. Copied; NULL clears it. */
struct yetty_ycore_void_result yetty_ygui_ynode_set_title(struct yetty_ygui_object *node,
                                                          const char *title);

/* Graph-space position (the editor maps it to a screen rect via the
 * current pan/zoom). Setting it re-applies the node's screen layout. */
struct yetty_ycore_void_result yetty_ygui_ynode_set_graph_pos(struct yetty_ygui_object *node,
                                                              float gx, float gy);
void yetty_ygui_ynode_graph_pos(const struct yetty_ygui_object *node, float *gx, float *gy);

/* Graph-space size of the node box. */
struct yetty_ycore_void_result yetty_ygui_ynode_set_graph_size(struct yetty_ygui_object *node,
                                                               float gw, float gh);
void yetty_ygui_ynode_graph_size(const struct yetty_ygui_object *node, float *gw, float *gh);

/* Add an input / output pin. Returns the new pin's index. */
struct uint32_result yetty_ygui_ynode_add_input(struct yetty_ygui_object *node, const char *name);
struct uint32_result yetty_ygui_ynode_add_output(struct yetty_ygui_object *node, const char *name);

int yetty_ygui_ynode_input_count(const struct yetty_ygui_object *node);
int yetty_ygui_ynode_output_count(const struct yetty_ygui_object *node);

/* Screen-space center of pin `index` on the given side (`output` != 0 →
 * right edge, else left edge). Returns 1 and writes (x,y) if the pin
 * exists, else 0. Valid only after a layout pass assigned the node rect. */
int yetty_ygui_ynode_pin_pos(const struct yetty_ygui_object *node, int output, int index, float *x,
                             float *y);

/* Hit-test: if (x,y) (screen) lands on a pin, write its side into
 * *output (0 = input, 1 = output) and its index into *index, and return
 * 1; otherwise return 0. */
int yetty_ygui_ynode_pin_at(const struct yetty_ygui_object *node, float x, float y, int *output,
                            int *index);

/* Recompute the node's screen layout from its graph rect + the editor's
 * current view. The editor calls this on pan/zoom; apps rarely need it. */
struct yetty_ycore_void_result yetty_ygui_ynode_reflow(struct yetty_ygui_object *node);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
