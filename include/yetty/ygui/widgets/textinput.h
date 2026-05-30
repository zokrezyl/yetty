/* GENERATED — do not edit. */
/* Public interface for regular class(es) `textinput` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TEXTINPUT_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TEXTINPUT_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_textinput_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
struct yetty_ycore_void_result yetty_ygui_textinput_set_text(struct yetty_ygui_object *obj,
                                                             const char *text);
const char *yetty_ygui_textinput_get_text(const struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_textinput_set_placeholder(struct yetty_ygui_object *obj,
                                                                    const char *placeholder);
struct yetty_ycore_void_result yetty_ygui_textinput_set_focus(struct yetty_ygui_object *obj,
                                                              int focused);

/* Feed one decoded key/char from the framework. Printable ASCII (32..126)
 * inserts at the cursor; 0x08/0x7F deletes the previous char. Returns
 * 1 if the key was consumed. */
int yetty_ygui_textinput_handle_key(struct yetty_ygui_object *obj, uint32_t key);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
