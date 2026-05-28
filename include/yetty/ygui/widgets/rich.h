/* GENERATED — do not edit. */
/* Public interface for regular class(es) `rich` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_RICH_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_RICH_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_rich_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
/* Wipe every line + every span. */
struct yetty_ycore_void_result yetty_ygui_rich_clear(struct yetty_ygui_object *obj);

/* Start a new line. The first span added after this call goes on the
 * new line. Subsequent rich_add_span calls stack horizontally on the
 * same line until the next rich_add_line. */
struct yetty_ycore_void_result yetty_ygui_rich_add_line(struct yetty_ygui_object *obj);

/* Append a styled span on the current line. `text` is copied. */
struct yetty_ycore_void_result yetty_ygui_rich_add_span(struct yetty_ygui_object *obj,
                                                        const char *text, float font_size,
                                                        uint32_t color_rgba);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
