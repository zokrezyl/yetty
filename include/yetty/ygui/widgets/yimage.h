/* GENERATED — do not edit. */
/* Public interface for regular class(es) `yimage` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YIMAGE_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YIMAGE_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_yimage_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
/* Replace the widget's content with `bytes`. The widget makes its own
 * copy; the caller may free its buffer immediately after the call.
 * Passing NULL/0 clears the content. Marks the widget dirty so the
 * next emit ships fresh body bytes. */
struct yetty_ycore_void_result yetty_ygui_yimage_set_bytes(struct yetty_ygui_object *obj,
                                                           const uint8_t *bytes, size_t len);

/* Direct accessors — caller must not mutate the returned buffer. */
const uint8_t *yetty_ygui_yimage_bytes(const struct yetty_ygui_object *obj);
size_t yetty_ygui_yimage_bytes_len(const struct yetty_ygui_object *obj);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
