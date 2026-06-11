/* GENERATED — do not edit. */
/* Public interface for regular class(es) `label` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_LABEL_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_LABEL_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_label_class_get(void);

struct yetty_ygui_object;
struct yetty_ygui_label;
YETTY_YRESULT_DECLARE(yetty_ygui_label_data_ptr, struct yetty_ygui_label *);
struct yetty_ygui_label_data_ptr_result yetty_ygui_label_data(struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_label_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_rgba;
struct yetty_ygui_object;

struct yetty_ycore_void_result yetty_ygui_label_set_text(struct yetty_ygui_object *obj,
                                                         const char *text);
struct yetty_ycore_const_char_ptr_result yetty_ygui_label_get_text(
    const struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_label_set_font_size(struct yetty_ygui_object *obj,
                                                              float size_px);
struct yetty_ycore_void_result yetty_ygui_label_set_color(struct yetty_ygui_object *obj,
                                                          struct yetty_ycore_rgba color);

#endif
