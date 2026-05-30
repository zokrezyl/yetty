/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ydiagram` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YDIAGRAM_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YDIAGRAM_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_ydiagram_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
struct yetty_ygui_object;

/* Set the Mermaid source. The widget renders it to a ydraw buffer
 * immediately (the diagram's layout has an intrinsic size independent of
 * the widget rect); the embedded buffer's primitives are painted into the
 * surrounding ygrid, offset by the widget's position, on the next emit.
 * Passing NULL/empty clears the diagram.
 *
 * Returns the render error (parse / layout failure) if the source is not
 * valid Mermaid; the source string is still retained so a getter can
 * surface it, but nothing is drawn. */
struct yetty_ycore_void_result yetty_ygui_ydiagram_set_source(struct yetty_ygui_object *obj,
                                                              const char *source);

/* The current Mermaid source, or NULL if none set. Caller must not free
 * or mutate the returned string. */
const char *yetty_ygui_ydiagram_get_source(const struct yetty_ygui_object *obj);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
