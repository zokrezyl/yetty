/* ygui-yvideo.h — figure widget wrapping YETTY_YFIGURE_KIND_YVIDEO.
 * Same shape as yimage: emit_container mints the figure, emit_body
 * ships raw bytes. */
#ifndef YETTY_YGUI_WIDGETS_YVIDEO_H
#define YETTY_YGUI_WIDGETS_YVIDEO_H
#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>
#ifdef __cplusplus
extern "C" {
#endif
const struct yetty_ygui_class *yetty_ygui_yvideo_class_get(void);
struct yetty_ycore_void_result yetty_ygui_yvideo_set_bytes(struct yetty_ygui_object *obj,
                                                           const uint8_t *bytes, size_t len);
#ifdef __cplusplus
}
#endif
#endif
