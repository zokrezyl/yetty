/*
 * ygui-dialog.h — absolute-positioned modal-style panel with title bar
 * + body. The app adds body widgets as children via yetty_ygui_add.
 *
 * Inherits vbox: header strip (drawn by dialog.paint) then children
 * laid out top-to-bottom inside the padded body.
 */
#ifndef YETTY_YGUI_WIDGETS_DIALOG_H
#define YETTY_YGUI_WIDGETS_DIALOG_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_dialog_class_get(void);

struct yetty_ycore_void_result yetty_ygui_dialog_set_title(struct yetty_ygui_object *obj,
                                                           const char *title);

struct yetty_ycore_void_result yetty_ygui_dialog_open_at(struct yetty_ygui_object *obj, float x,
                                                         float y, float width, float height);
struct yetty_ycore_void_result yetty_ygui_dialog_close(struct yetty_ygui_object *obj);
int yetty_ygui_dialog_is_open(const struct yetty_ygui_object *obj);

#ifdef __cplusplus
}
#endif

#endif
