/* GENERATED — do not edit. */
/* Object API for regular class(es) `scrollarea` (implementation module: ygui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YGUI_WIDGETS_SCROLLAREA_H
#define YETTY_YCLASSGEN_API_YGUI_WIDGETS_SCROLLAREA_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_scrollarea;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_SCROLLAREA_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_SCROLLAREA_PTR_RESULT
struct yetty_ygui_scrollarea_ptr_result {
    int ok;
    union {
        struct yetty_ygui_scrollarea *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_scrollarea_ptr_result yetty_ygui_scrollarea_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_scrollarea_to(struct yetty_ygui_scrollarea *data);

struct yetty_yclass_object_ptr_result yetty_ygui_scrollarea_create(struct yetty_yclass_ctx *ctx);

/* Switch this scrollarea to RETAINED-SCENE mode: its figure kind becomes
 * "yscene" (must be called before the first emit mints the figure). The
 * receiver keeps the whole document across bodies and scrolls it on the
 * GPU — content inside is emitted in document space, scrolling ships a
 * SET_CHILD_SCROLL instead of a body, and the gutter scrollbar is not
 * painted. The gutter padding is released back to the content. */
struct yetty_ycore_void_result yetty_ygui_scrollarea_enable_scene(struct yetty_yclass_object *obj);
/* Programmatic scroll (navigation reset, restore). Clamped like every
 * other scroll path. */
struct yetty_ycore_void_result yetty_ygui_scrollarea_scroll_set(struct yetty_yclass_object *obj,
                                                                float offset);
struct yetty_ycore_float_result yetty_ygui_scrollarea_scroll_get(
    const struct yetty_yclass_object *obj);

#ifdef __cplusplus
}
#endif

#endif
