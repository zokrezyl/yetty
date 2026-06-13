/* GENERATED — do not edit. */
/* Public interface for regular class(es) `button` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_BUTTON_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_BUTTON_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_object;

struct yetty_ygui_button;

struct yetty_ygui_button_ptr_result {
    int ok;
    union {
        struct yetty_ygui_button *value;
        struct yetty_ycore_error error;
    };
};

struct yetty_yclass_ptr_result yetty_ygui_button_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_button_ptr_result yetty_ygui_button_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_button_to(struct yetty_ygui_button *data);

struct yetty_yclass_object_ptr_result yetty_ygui_button_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_button_set_label(struct yetty_yclass_object *obj,
                                                           const char *label);
struct yetty_ycore_void_result yetty_ygui_button_set_chrome_icon(struct yetty_yclass_object *obj,
                                                                 int kind);
struct yetty_ycore_const_char_ptr_result yetty_ygui_button_get_label(
    const struct yetty_yclass_object *obj);

#endif
