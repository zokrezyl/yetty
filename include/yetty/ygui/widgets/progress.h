/*
 * ygui-progress.h — horizontal progress bar.
 *
 * Value in [0, 1]. No interaction.
 */
#ifndef YETTY_YGUI_WIDGETS_PROGRESS_H
#define YETTY_YGUI_WIDGETS_PROGRESS_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_progress_class_get(void);

struct yetty_ycore_void_result yetty_ygui_progress_set_value(struct yetty_ygui_object *obj,
                                                             float value);
float yetty_ygui_progress_get_value(const struct yetty_ygui_object *obj);

#ifdef __cplusplus
}
#endif

#endif
