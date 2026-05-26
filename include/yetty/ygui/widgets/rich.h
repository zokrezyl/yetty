/*
 * ygui-rich.h — styled multi-line text widget.
 *
 * Holds a sequence of "lines"; each line is a sequence of "spans" with
 * their own text + font_size + color. Paint walks lines top-to-bottom,
 * spans left-to-right with simple monospace cursor advance.
 *
 * Used for the Welcome / Code tab bodies and any text that needs
 * inline color highlighting (keywords, comments, strings).
 */
#ifndef YETTY_YGUI_WIDGETS_RICH_H
#define YETTY_YGUI_WIDGETS_RICH_H

#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_rich_class_get(void);

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

#ifdef __cplusplus
}
#endif

#endif
