/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ydraw_embed` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YDRAW_EMBED_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YDRAW_EMBED_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_ydraw_embed_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
/* Replace the embedded draw_list. Takes ownership — the widget will
 * destroy the buffer on next replace or on destruction. NULL clears. */
struct yetty_ycore_void_result yetty_ygui_ydraw_embed_set_buffer(struct yetty_ygui_object *obj,
                                                                 struct yetty_ydraw_draw_list *buf);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
