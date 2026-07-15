/* GENERATED — do not edit. */
/* Public interface for regular class(es) `13_color_mixer` (module: demoygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_DEMOYGUI_13_COLOR_MIXER_MAIN_H
#define YETTY_YCLASSGEN_DEMOYGUI_13_COLOR_MIXER_MAIN_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Demo app class: a yguiapp:app subclass with no extra state. */
struct yetty_yclass_ptr_result yetty_demoygui_13_color_mixer_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_demoygui_13_color_mixer;
struct yetty_demoygui_13_color_mixer_ptr_result {
    int ok;
    union {
        struct yetty_demoygui_13_color_mixer *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_demoygui_13_color_mixer_ptr_result yetty_demoygui_13_color_mixer_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_demoygui_13_color_mixer_to(struct yetty_demoygui_13_color_mixer *data);

struct yetty_yclass_object_ptr_result yetty_demoygui_13_color_mixer_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_demoygui_register(void);

#ifdef __cplusplus
}
#endif

#endif
