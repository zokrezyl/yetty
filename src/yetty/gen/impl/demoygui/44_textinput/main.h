/* GENERATED — do not edit. */
/* Public interface for regular class(es) `44_textinput` (module: demoygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_DEMOYGUI_44_TEXTINPUT_MAIN_H
#define YETTY_YCLASSGEN_DEMOYGUI_44_TEXTINPUT_MAIN_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Demo app class: holds the edit box, the live labels, and a copy buffer. */
struct yetty_yclass_ptr_result yetty_demoygui_44_textinput_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_demoygui_44_textinput;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_DEMOYGUI_44_TEXTINPUT_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_DEMOYGUI_44_TEXTINPUT_PTR_RESULT
struct yetty_demoygui_44_textinput_ptr_result {
    int ok;
    union {
        struct yetty_demoygui_44_textinput *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_demoygui_44_textinput_ptr_result yetty_demoygui_44_textinput_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_demoygui_44_textinput_to(
    struct yetty_demoygui_44_textinput *data);

struct yetty_yclass_object_ptr_result yetty_demoygui_44_textinput_create(
    struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_demoygui_register(void);

#ifdef __cplusplus
}
#endif

#endif
