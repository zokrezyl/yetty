/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ydraw_embed` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YDRAW_EMBED_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YDRAW_EMBED_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_ydraw_embed_class_get(void);

struct yetty_ygui_object;
struct embed_data;
YETTY_YRESULT_DECLARE(yetty_ygui_ydraw_embed_data_ptr, struct embed_data *);
struct yetty_ygui_ydraw_embed_data_ptr_result yetty_ygui_ydraw_embed_data(struct yetty_ygui_object *obj);

struct yetty_ycore_void_result yetty_ygui_ydraw_embed_set_buffer(struct yetty_ygui_object *obj, struct yetty_ydraw_draw_list *buf);

#endif
