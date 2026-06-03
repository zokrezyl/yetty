/* GENERATED — do not edit. */
/* Public interface for mixin(es) `clickable` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YGUI_MIXINS_CLICKABLE_H
#define YETTY_YCLASSGEN_YGUI_MIXINS_CLICKABLE_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_clickable_mixin_get(void);

struct yetty_ygui_object;
struct clickable_data;
YETTY_YRESULT_DECLARE(yetty_ygui_clickable_data_ptr, struct clickable_data *);
struct yetty_ygui_clickable_data_ptr_result yetty_ygui_clickable_data(struct yetty_ygui_object *obj);

struct yetty_ygui_object;

typedef struct yetty_ycore_void_result (*yetty_ygui_click_cb)(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              void *userdata);
struct yetty_ycore_void_result yetty_ygui_clickable_on_click_set(struct yetty_ygui_object *obj, yetty_ygui_click_cb cb, void *userdata);
int yetty_ygui_clickable_is_pressed(const struct yetty_ygui_object *obj);
void yetty_ygui_clickable_press_pos(const struct yetty_ygui_object *obj, float *x, float *y);

#endif
