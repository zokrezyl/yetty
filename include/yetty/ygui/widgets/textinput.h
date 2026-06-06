/* GENERATED — do not edit. */
/* Public interface for regular class(es) `textinput` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TEXTINPUT_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TEXTINPUT_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_textinput_class_get(void);

struct yetty_ygui_object;
struct textinput_data;
YETTY_YRESULT_DECLARE(yetty_ygui_textinput_data_ptr, struct textinput_data *);
struct yetty_ygui_textinput_data_ptr_result yetty_ygui_textinput_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_textinput_set_text(struct yetty_ygui_object *obj, const char *text);
struct yetty_ycore_const_char_ptr_result yetty_ygui_textinput_get_text(const struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_textinput_set_placeholder(struct yetty_ygui_object *obj, const char *placeholder);
struct yetty_ycore_void_result yetty_ygui_textinput_set_focus(struct yetty_ygui_object *obj, int focused);
int yetty_ygui_textinput_handle_key(struct yetty_ygui_object *obj, uint32_t key);

#endif
