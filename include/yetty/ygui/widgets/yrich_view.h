/* GENERATED — do not edit. */
/* Public interface for regular class(es) `yrich_view` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YRICH_VIEW_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YRICH_VIEW_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrich/yrich-types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yclass_ptr_result yetty_ygui_yrich_view_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_yrich_view;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_YRICH_VIEW_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_YRICH_VIEW_PTR_RESULT
struct yetty_ygui_yrich_view_ptr_result {
    int ok;
    union {
        struct yetty_ygui_yrich_view *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_yrich_view_ptr_result yetty_ygui_yrich_view_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_yrich_view_to(struct yetty_ygui_yrich_view *data);

struct yetty_yclass_object_ptr_result yetty_ygui_yrich_view_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

/*-----------------------------------------------------------------------------
 * Public API — model attachment + host-driven keyboard forwarding.
 *---------------------------------------------------------------------------*/
struct yetty_ycore_void_result yetty_ygui_yrich_view_set_document(struct yetty_yclass_object *obj,
                                                                  struct yetty_yclass_object *doc,
                                                                  int own);
/* Force a re-render at the next emit even when neither the rect nor the
 * document's own dirty flag changed — used after a host action (toolbar
 * button, programmatic edit) mutates the document. */
struct yetty_ycore_void_result yetty_ygui_yrich_view_invalidate(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_yrich_view_document(
    const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_yrich_view_content_size(
    const struct yetty_yclass_object *obj, float *w, float *h);
/* Re-fit the widget's authored size to the document's content extent so
 * the surrounding scrollarea tracks growth/shrink after edits. */
struct yetty_ycore_void_result yetty_ygui_yrich_view_fit_content(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_yrich_view_feed_key(struct yetty_yclass_object *obj,
                                                              uint32_t key, uint32_t mods);
struct yetty_ycore_void_result yetty_ygui_yrich_view_feed_text(struct yetty_yclass_object *obj,
                                                               const char *text, size_t len);
/* Forward a double-click at framework-space (x,y) to the document (word
 * select + word-drag arming). Returns 1 if the point was inside the view and
 * the click was consumed, 0 otherwise — the platform delivers double-click
 * separately from the press/motion path, so the host routes it here. */
struct yetty_ycore_int_result yetty_ygui_yrich_view_feed_double_click(
    struct yetty_yclass_object *obj, float x, float y, int button);

#ifdef __cplusplus
}
#endif

#endif
