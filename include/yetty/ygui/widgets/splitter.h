/* GENERATED — do not edit. */
/* Public interface for regular class(es) `splitter` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_SPLITTER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_SPLITTER_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_splitter_class_get(void);

struct yetty_ygui_object;
struct yetty_ygui_splitter;
YETTY_YRESULT_DECLARE(yetty_ygui_splitter_data_ptr, struct yetty_ygui_splitter *);
struct yetty_ygui_splitter_data_ptr_result yetty_ygui_splitter_data(struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_splitter_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_object;

struct yetty_ygui_object;
typedef void (*yetty_ygui_splitter_change_cb)(struct yetty_ygui_object *splitter, float delta,
                                              void *userdata);
struct yetty_ycore_void_result yetty_ygui_splitter_set_axis(struct yetty_ygui_object *obj, int row);
struct yetty_ycore_int_result yetty_ygui_splitter_get_axis(const struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_splitter_set_min(struct yetty_ygui_object *obj,
                                                           float min_size);
struct yetty_ycore_void_result yetty_ygui_splitter_on_change(struct yetty_ygui_object *obj,
                                                             yetty_ygui_splitter_change_cb cb,
                                                             void *userdata);

#endif
