/*
 * ygui-textinput.h — single-line text input.
 *
 * Receives byte stream via the framework key callback. Apps wire the
 * framework's key_cb to call yetty_ygui_textinput_handle_key on the
 * focused widget; this widget exposes a focus flag the app toggles
 * (via clickable on_click).
 */
#ifndef YETTY_YGUI_WIDGETS_TEXTINPUT_H
#define YETTY_YGUI_WIDGETS_TEXTINPUT_H

#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_textinput_class_get(void);

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

#ifdef __cplusplus
}
#endif

#endif
