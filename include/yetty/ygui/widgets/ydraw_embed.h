/*
 * ygui-ydraw_embed.h — base widget that hosts a yetty_ydraw_draw_list
 * and paints it translated by the widget's own rect origin.
 *
 * Used as the base class for content-producer widgets (ymarkdown,
 * ybrowser, ypdf, yzoo, yjungle): they each build a draw_list from
 * their source, hand it to this base via _set_buffer, and rely on
 * the inherited paint to position it inside the layout.
 *
 * The buffer ownership transfers in — the base destroys it on
 * destruction or on subsequent _set_buffer.
 */
#ifndef YETTY_YGUI_WIDGETS_YDRAW_EMBED_H
#define YETTY_YGUI_WIDGETS_YDRAW_EMBED_H

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_draw_list;

const struct yetty_ygui_class *yetty_ygui_ydraw_embed_class_get(void);

/* Replace the embedded draw_list. Takes ownership — the widget will
 * destroy the buffer on next replace or on destruction. NULL clears. */
struct yetty_ycore_void_result yetty_ygui_ydraw_embed_set_buffer(struct yetty_ygui_object *obj,
                                                                 struct yetty_ydraw_draw_list *buf);

#ifdef __cplusplus
}
#endif

#endif
