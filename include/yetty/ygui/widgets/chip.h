/* ygui-chip.h — small pill label with optional close x. */
#ifndef YETTY_YGUI_WIDGETS_CHIP_H
#define YETTY_YGUI_WIDGETS_CHIP_H
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_chip_class_get(void);
struct yetty_ycore_void_result yetty_ygui_chip_set_label(struct yetty_ygui_object *obj,
                                                         const char *label);
struct yetty_ycore_void_result yetty_ygui_chip_set_closable(struct yetty_ygui_object *obj,
                                                            int closable);
#ifdef __cplusplus
}
#endif
#endif
