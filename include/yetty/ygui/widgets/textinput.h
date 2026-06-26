/* GENERATED — do not edit. */
/* Public interface for regular class(es) `textinput` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_TEXTINPUT_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_TEXTINPUT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ygui_textinput_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_textinput;
struct yetty_ygui_textinput_ptr_result {
    int ok;
    union {
        struct yetty_ygui_textinput *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_ygui_textinput_ptr_result yetty_ygui_textinput_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_textinput_to(struct yetty_ygui_textinput *data);

struct yetty_yclass_object_ptr_result yetty_ygui_textinput_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_textinput_set_text(struct yetty_yclass_object *obj, const char *text);
struct yetty_ycore_const_char_ptr_result yetty_ygui_textinput_get_text(const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_textinput_set_placeholder(struct yetty_yclass_object *obj, const char *placeholder);
struct yetty_ycore_void_result yetty_ygui_textinput_set_focus(struct yetty_yclass_object *obj, int focused);
struct yetty_ycore_int_result yetty_ygui_textinput_handle_key(struct yetty_yclass_object *obj, uint32_t key);

#ifdef __cplusplus
}
#endif

#endif
