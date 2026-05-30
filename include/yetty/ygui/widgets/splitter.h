/* GENERATED — do not edit. */
/* Public interface for regular class(es) `splitter` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_SPLITTER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_SPLITTER_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_splitter_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
struct yetty_ygui_object;

/* External-drive drag callback. `delta` is the cursor's pixel offset
 * from the bar's centre along the resize axis at the moment of the
 * drag. The host repositions the bar on the next frame. */
typedef void (*yetty_ygui_splitter_change_cb)(struct yetty_ygui_object *splitter, float delta,
                                              void *userdata);

/* Force the bar orientation: row != 0 selects a row-bar (a vertical
 * divider between side-by-side siblings → horizontal resize); row == 0
 * selects a column-bar (horizontal divider → vertical resize). Without
 * an override the bar derives its axis from the parent's flex direction.
 * No-op on a non-splitter object. */
void yetty_ygui_splitter_set_axis(struct yetty_ygui_object *obj, int row);

/* Current axis: 1 = row-bar, 0 = column-bar, -1 = auto (unset) or `obj`
 * is not a splitter. Safe to call on any object. */
int yetty_ygui_splitter_get_axis(const struct yetty_ygui_object *obj);

/* Minimum size of the resized sibling in flex mode (no-op on non-splitter). */
void yetty_ygui_splitter_set_min(struct yetty_ygui_object *obj, float min_size);

/* Install the external-drive change callback. Setting a non-NULL cb
 * switches the splitter into external-drive mode (it reports a delta on
 * drag instead of resizing its flex siblings). No-op on non-splitter. */
void yetty_ygui_splitter_on_change(struct yetty_ygui_object *obj,
                                   yetty_ygui_splitter_change_cb cb, void *userdata);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
