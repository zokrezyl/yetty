/* GENERATED — do not edit. */
/* Public interface for regular class(es) `window` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_WINDOW_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_WINDOW_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ygui_window_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_window;
struct yetty_ygui_window_ptr_result {
    int ok;
    union {
        struct yetty_ygui_window *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_ygui_window_ptr_result yetty_ygui_window_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_window_to(struct yetty_ygui_window *data);

struct yetty_yclass_object_ptr_result yetty_ygui_window_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_yclass_object_ptr_result yetty_ygui_window_body(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_window_set_title(struct yetty_yclass_object *obj, const char *title);
struct yetty_ycore_void_result yetty_ygui_window_set_menu(struct yetty_yclass_object *obj, struct yetty_yclass_object *menu);
struct yetty_ycore_void_result yetty_ygui_window_set_closable(struct yetty_yclass_object *obj, int closable);
/* Chromeless: drop the title strip and shrink the top padding so the
 * body fills the frame. For docked/anchored panels (a status bar) the
 * titlebar is wasted space and the window isn't meant to be dragged. */
struct yetty_ycore_void_result yetty_ygui_window_set_chromeless(struct yetty_yclass_object *obj, int chromeless);

#ifdef __cplusplus
}
#endif

#endif
