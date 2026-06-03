/* GENERATED — do not edit. */
/* Public interface for regular class(es) `splitter` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_SPLITTER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_SPLITTER_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_splitter_class_get(void);

struct yetty_ygui_object;
struct splitter_data;
YETTY_YRESULT_DECLARE(yetty_ygui_splitter_data_ptr, struct splitter_data *);
struct yetty_ygui_splitter_data_ptr_result yetty_ygui_splitter_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;
typedef void (*yetty_ygui_splitter_change_cb)(struct yetty_ygui_object *splitter, float delta,
                                              void *userdata);
void yetty_ygui_splitter_set_axis(struct yetty_ygui_object *obj, int row);
int yetty_ygui_splitter_get_axis(const struct yetty_ygui_object *obj);
void yetty_ygui_splitter_set_min(struct yetty_ygui_object *obj, float min_size);
void yetty_ygui_splitter_on_change(struct yetty_ygui_object *obj, yetty_ygui_splitter_change_cb cb, void *userdata);

#endif
