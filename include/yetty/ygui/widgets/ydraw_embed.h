/* GENERATED — do not edit. */
/* Public interface for regular class(es) `ydraw_embed` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YDRAW_EMBED_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YDRAW_EMBED_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_ydraw_embed_class_get(void);

struct yetty_ygui_object;
struct yetty_ygui_ydraw_embed;
YETTY_YRESULT_DECLARE(yetty_ygui_ydraw_embed_data_ptr, struct yetty_ygui_ydraw_embed *);
struct yetty_ygui_ydraw_embed_data_ptr_result yetty_ygui_ydraw_embed_data(
    struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_ydraw_embed_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ydraw_drawable_list;
struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_ydraw_embed_set_buffer(
    struct yetty_ygui_object *obj, struct yetty_ydraw_drawable_list *buf);

#endif
