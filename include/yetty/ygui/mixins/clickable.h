/* GENERATED — do not edit. */
/* Public interface for mixin(es) `clickable` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_MIXINS_CLICKABLE_H
#define YETTY_YCLASSGEN_YGUI_MIXINS_CLICKABLE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_clickable_mixin_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_clickable;
YETTY_YRESULT_DECLARE(yetty_ygui_clickable_ptr, struct yetty_ygui_clickable *);
struct yetty_ygui_clickable_ptr_result yetty_ygui_clickable_from(struct yetty_yclass_object *obj);

struct yetty_ycore_int_result;

struct yetty_ycore_int_result yetty_ygui_widget_on_press(struct yetty_yclass_ctx * yclass_ctx, struct yetty_yclass_object * yclass_obj, float x, float y, int button);
struct yetty_ycore_int_result yetty_ygui_widget_on_release(struct yetty_yclass_ctx * yclass_ctx, struct yetty_yclass_object * yclass_obj, float x, float y, int button);

typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_press_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, int);
typedef struct yetty_ycore_int_result (*yetty_ygui_widget_on_release_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, int);

struct yetty_ycore_void_result yetty_ygui_register(void);

typedef struct yetty_ycore_void_result (*yetty_ygui_click_cb)(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              void *userdata);
struct yetty_ycore_void_result yetty_ygui_clickable_on_click_set(struct yetty_yclass_object *obj, yetty_ygui_click_cb cb, void *userdata);
struct yetty_ycore_int_result yetty_ygui_clickable_is_pressed(const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_clickable_press_pos(const struct yetty_yclass_object *obj, float *x, float *y);

#endif
