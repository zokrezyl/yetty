/* GENERATED — do not edit. */
/* Public interface for regular class(es) `slider` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_SLIDER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_SLIDER_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_object;

struct yetty_ygui_slider;

struct yetty_ygui_slider_ptr_result {
    int ok;
    union {
        struct yetty_ygui_slider *value;
        struct yetty_ycore_error error;
    };
};

struct yetty_yclass_ptr_result yetty_ygui_slider_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_slider_ptr_result yetty_ygui_slider_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_slider_to(struct yetty_ygui_slider *data);

struct yetty_yclass_object_ptr_result yetty_ygui_slider_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_slider_set_range(struct yetty_yclass_object *obj,
                                                           float min, float max);
struct yetty_ycore_void_result yetty_ygui_slider_set_value(struct yetty_yclass_object *obj,
                                                           float value);
struct yetty_ycore_float_result yetty_ygui_slider_get_value(const struct yetty_yclass_object *obj);

#endif
