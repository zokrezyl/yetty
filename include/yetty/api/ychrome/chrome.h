/* GENERATED — do not edit. */
/* Object API for regular class(es) `chrome` (implementation module: ychrome).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YCHROME_CHROME_H
#define YETTY_YCLASSGEN_API_YCHROME_CHROME_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yui_event;

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YCHROME_FLAG
#define YETTY_YCLASSGEN_TYPE_YETTY_YCHROME_FLAG
/* Feature flags for configure(). OR them together; FLAG_ALL enables the lot.
 * Defined here in the owning .c; codegen reproduces the enum into the generated
 * chrome.h for consumers. */
enum yetty_ychrome_flag {
    YETTY_YCHROME_FLAG_DRAG = 1,
    YETTY_YCHROME_FLAG_RESIZE = 2,
    YETTY_YCHROME_FLAG_MAXIMIZE = 4,
    YETTY_YCHROME_FLAG_ALL = 7,
};
#endif



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ychrome_chrome;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YCHROME_CHROME_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YCHROME_CHROME_PTR_RESULT
struct yetty_ychrome_chrome_ptr_result {
    int ok;
    union {
        struct yetty_ychrome_chrome *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ychrome_chrome_ptr_result yetty_ychrome_chrome_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ychrome_chrome_to(struct yetty_ychrome_chrome *data);

/* Bind the engine to a window_chrome and set the caption/edge geometry. Call
 * once after create(), before feeding events. window_chrome is borrowed. */
struct yetty_ycore_void_result yetty_ychrome_configure(struct yetty_yclass_object * obj, struct yetty_yclass_object * window_chrome, float caption_height, float edge_size, uint32_t flags);
/* Update the window size so the right/bottom edge bands track it. Call on every
 * resize. */
struct yetty_ycore_void_result yetty_ychrome_set_size(struct yetty_yclass_object * obj, float width, float height);
struct yetty_ycore_void_result yetty_ychrome_destroy(struct yetty_yclass_object * obj);
struct yetty_ycore_int_result yetty_ychrome_edge_cursor_at(struct yetty_yclass_object * obj, float x, float y);
/* Paint the caption strip into a fresh drawable list and return it: a filled
 * background spanning (width × caption_height) plus the minimize/maximize/close
 * glyphs flush right. The caller composites the list as a pinned top figure and
 * destroys it. Uses font_id=-1 (the default font), so no font dependency. */
struct yetty_ydraw_drawable_list_result yetty_ychrome_render(struct yetty_yclass_object * obj);
/* Feed one mouse event. Returns 1 if chrome claimed it (the app should stop
 * processing it), 0 if it's not a chrome gesture. Forward events here only
 * after your own controls (buttons, tabs) had their chance. */
struct yetty_ycore_int_result yetty_ychrome_handle_event(struct yetty_yclass_object * obj, const struct yetty_yui_event * event);

struct yetty_yclass_object_ptr_result yetty_ychrome_chrome_create(struct yetty_yclass_ctx *ctx);



/* Current window-control button under the pointer (1=minimize, 2=maximize,
 * 3=close; 0=none). Hosts poll this after forwarding an event and re-paint the
 * caption when it changes, so the hover highlight tracks the pointer. */
struct yetty_ycore_int_result yetty_ychrome_hover_button(struct yetty_yclass_object *obj);
/* 1 while a caption-drag or edge-resize gesture is live. Hosts that route
 * pointer events UI-first (per the header contract above) still hand chrome
 * the whole stream while this is set, so a live gesture cannot be stolen by
 * widgets the pointer happens to cross. */
struct yetty_ycore_int_result yetty_ychrome_in_gesture(struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
