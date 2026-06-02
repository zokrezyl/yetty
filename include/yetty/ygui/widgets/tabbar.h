/* GENERATED — do not edit. */
/* Public interface for regular class(es) `tabbar` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TABBAR_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TABBAR_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_tabbar_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
struct yetty_ygui_object;

/* Append a tab header to the tabbar. Returns the new header widget,
 * which the app can pass to layout setters or hide / show. The header
 * itself is a chrome widget so its rect / layout fields work the
 * normal way. */
struct yetty_ygui_object_ptr_result yetty_ygui_tabbar_add_tab(struct yetty_ygui_object *tabbar,
                                                              const char *label);

/* Remove a tab header. Returns OK_VOID if `index` is out of range. */
struct yetty_ycore_void_result yetty_ygui_tabbar_remove_tab(struct yetty_ygui_object *tabbar,
                                                            int index);

/* Replace the label of the tab at `index` (e.g. to show a page title once
 * it is known). No-op if `index` is out of range. NULL clears the label. */
struct yetty_ycore_void_result yetty_ygui_tabbar_set_label(struct yetty_ygui_object *tabbar,
                                                           int index, const char *label);

int yetty_ygui_tabbar_count(const struct yetty_ygui_object *tabbar);

int yetty_ygui_tabbar_active(const struct yetty_ygui_object *tabbar);

/* Set the active tab. Out-of-range values are clamped. Emits a
 * VALUE_CHANGED event with i0 = new_index. */
struct yetty_ycore_void_result yetty_ygui_tabbar_set_active(struct yetty_ygui_object *tabbar,
                                                            int index);

/* Per-tab close affordance. Installing a non-NULL callback makes each
 * pill paint a close-x at its right edge; a click landing there fires
 * the callback with that tab's index instead of activating it. The host
 * decides what to remove (it typically owns the tab model and mirrors
 * the new count back via add/remove). */
typedef void (*yetty_ygui_tab_close_cb)(struct yetty_ygui_object *tabbar, int index, void *userdata);

struct yetty_ycore_void_result yetty_ygui_tabbar_set_on_close(struct yetty_ygui_object *tabbar,
                                                             yetty_ygui_tab_close_cb cb,
                                                             void *userdata);

/* Built-in "+" new-tab affordance. Installing a non-NULL callback makes
 * the tabbar paint a "+" immediately to the right of the rightmost tab
 * (not glued to the far edge) and fire the callback when it is clicked.
 * The host typically owns the tab model and mirrors the new tab back via
 * add/remove. Pass NULL to remove the affordance. */
typedef void (*yetty_ygui_tab_new_cb)(struct yetty_ygui_object *tabbar, void *userdata);

struct yetty_ycore_void_result yetty_ygui_tabbar_set_on_new_tab(struct yetty_ygui_object *tabbar,
                                                                yetty_ygui_tab_new_cb cb,
                                                                void *userdata);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
