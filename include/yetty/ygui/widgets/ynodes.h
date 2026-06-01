/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ynodes` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YNODES_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YNODES_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_ynodes_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
#include <yetty/ycore/result.h>
#include <yetty/ygui/object.h>

/*-----------------------------------------------------------------------------
 * ynodes — a node-graph editor canvas.
 *
 * Hosts ynode children, draws a grid background and the connection links
 * between node pins, and owns the view (pan + zoom). Interaction:
 *   - drag the empty canvas  → pan
 *   - mouse wheel            → zoom toward the cursor
 *   - drag a node's chrome   → move the node (graph space)
 *   - drag from a pin to a   → create a link (output pin → input pin)
 *     compatible pin
 * Each ynode body can hold arbitrary ygui widgets, so a node is a small
 * form, not just a labelled box.
 *---------------------------------------------------------------------------*/

/* Convenience: create a ynode child at graph position (gx, gy) and return
 * it. Equivalent to yetty_ygui_add(yetty_ygui_ynode_class_get().value,
 * editor) followed by yetty_ygui_ynode_set_graph_pos. */
struct yetty_ygui_object_ptr_result yetty_ygui_ynodes_add_node(struct yetty_ygui_object *editor,
                                                               float gx, float gy);

/* Connect output pin `out_idx` of `from` to input pin `in_idx` of `to`.
 * Does nothing (returns OK) if such a link already exists. */
struct yetty_ycore_void_result yetty_ygui_ynodes_link(struct yetty_ygui_object *editor,
                                                      struct yetty_ygui_object *from, int out_idx,
                                                      struct yetty_ygui_object *to, int in_idx);

/* View: pan in screen pixels, zoom factor. */
void yetty_ygui_ynodes_view(const struct yetty_ygui_object *editor, float *pan_x, float *pan_y,
                            float *zoom);
struct yetty_ycore_void_result yetty_ygui_ynodes_set_view(struct yetty_ygui_object *editor,
                                                          float pan_x, float pan_y, float zoom);
float yetty_ygui_ynodes_zoom(const struct yetty_ygui_object *editor);

/* Re-apply the current view to every ynode child (recompute their screen
 * layout). Called internally on pan/zoom. */
struct yetty_ycore_void_result yetty_ygui_ynodes_reflow(struct yetty_ygui_object *editor);

/* Drop every link that references `node`. Called by a node's destructor
 * before it is freed so no dangling link survives. */
struct yetty_ycore_void_result yetty_ygui_ynodes_drop_links_for(struct yetty_ygui_object *editor,
                                                                struct yetty_ygui_object *node);

/*-----------------------------------------------------------------------------
 * Pending-link drag. Driven by a ynode while the user drags out of a pin:
 * begin on the pin press, update on each motion, end on release. Apps do
 * not normally call these directly.
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_ygui_ynodes_begin_link(struct yetty_ygui_object *editor,
                                                            struct yetty_ygui_object *from, int pin,
                                                            int output, float x, float y);
struct yetty_ycore_void_result yetty_ygui_ynodes_update_link(struct yetty_ygui_object *editor,
                                                             float x, float y);
struct yetty_ycore_void_result yetty_ygui_ynodes_end_link(struct yetty_ygui_object *editor, float x,
                                                          float y);

/*-----------------------------------------------------------------------------
 * Insertable-widget palette. Register a widget class and the node's
 * right-click menu offers "Add <label>"; choosing it instantiates `cls`
 * as a child of that node (and grows the node to fit). `label` is copied;
 * `cls` is borrowed (typically a `*_class_get().value`). This keeps the
 * editor widget-agnostic — the app decides what "any widget" means.
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_ygui_ynodes_register_widget(struct yetty_ygui_object *editor,
                                                                 const char *label,
                                                                 const struct yetty_yclass *cls);

/*-----------------------------------------------------------------------------
 * Right-click context menus. The editor owns a single popup_menu (created
 * lazily) that it repopulates per open. The editor opens the canvas menu
 * on a right-press over empty canvas; a ynode opens the node menu on a
 * right-press over its chrome. Apps may also drive these directly.
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_ygui_ynodes_open_canvas_menu(struct yetty_ygui_object *editor,
                                                                  float x, float y);
struct yetty_ycore_void_result yetty_ygui_ynodes_open_node_menu(struct yetty_ygui_object *editor,
                                                                struct yetty_ygui_object *node,
                                                                float x, float y);
struct yetty_ycore_void_result yetty_ygui_ynodes_close_menu(struct yetty_ygui_object *editor);
int yetty_ygui_ynodes_menu_is_open(const struct yetty_ygui_object *editor);

/* Link-changed callback, fired after the user completes a connection. */
typedef struct yetty_ycore_void_result (*yetty_ygui_ynodes_link_cb)(
    struct yetty_ygui_object *editor, struct yetty_ygui_object *from, int out_idx,
    struct yetty_ygui_object *to, int in_idx, void *userdata);

struct yetty_ycore_void_result yetty_ygui_ynodes_on_link_set(struct yetty_ygui_object *editor,
                                                             yetty_ygui_ynodes_link_cb cb,
                                                             void *userdata);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
