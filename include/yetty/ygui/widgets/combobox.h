/* GENERATED — do not edit. */
/* Public interface for regular class(es) `combobox` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_COMBOBOX_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_COMBOBOX_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_object;

struct yetty_ygui_combobox;

struct yetty_ygui_combobox_ptr_result {
    int ok;
    union {
        struct yetty_ygui_combobox *value;
        struct yetty_ycore_error error;
    };
};

struct yetty_yclass_ptr_result yetty_ygui_combobox_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_combobox_ptr_result yetty_ygui_combobox_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_combobox_to(struct yetty_ygui_combobox *data);

struct yetty_yclass_object_ptr_result yetty_ygui_combobox_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_combobox_set_text(struct yetty_yclass_object *obj,
                                                            const char *t);
struct yetty_ycore_void_result yetty_ygui_combobox_add_suggestion(struct yetty_yclass_object *obj,
                                                                  const char *t);
struct yetty_ycore_void_result yetty_ygui_combobox_set_menu(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *menu);
struct yetty_ycore_const_char_ptr_result yetty_ygui_combobox_get_text(
    const struct yetty_yclass_object *obj);

#endif
