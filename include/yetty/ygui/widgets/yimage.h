/*
 * ygui-yimage.h — figure-shaped widget wrapping a raw image source.
 *
 * Figure widget (figure_kind == YETTY_YFIGURE_KIND_YIMAGE). On the wire,
 * the widget IS its own figure under the engine's container — the
 * engine emits CREATE_CHILD(kind=YIMAGE) in pass 1 and ships the
 * figure body (raw bytes) targeting obj->id in pass 2.
 *
 * The widget owns either a file path or an in-memory byte buffer; on
 * each emit the body bytes are sent verbatim to the receiver, where
 * the YIMAGE figure factory decodes them (stb_image) and renders.
 */
#ifndef YETTY_YGUI_WIDGETS_YIMAGE_H
#define YETTY_YGUI_WIDGETS_YIMAGE_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_yimage_class_get(void);

/* Replace the widget's content with `bytes`. The widget makes its own
 * copy; the caller may free its buffer immediately after the call.
 * Passing NULL/0 clears the content. Marks the widget dirty so the
 * next emit ships fresh body bytes. */
struct yetty_ycore_void_result yetty_ygui_yimage_set_bytes(struct yetty_ygui_object *obj,
                                                           const uint8_t *bytes, size_t len);

/* Direct accessors — caller must not mutate the returned buffer. */
const uint8_t *yetty_ygui_yimage_bytes(const struct yetty_ygui_object *obj);
size_t yetty_ygui_yimage_bytes_len(const struct yetty_ygui_object *obj);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YGUI_WIDGETS_YIMAGE_H */
