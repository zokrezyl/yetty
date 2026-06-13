/* GENERATED — do not edit. */
/* Public interface for regular class(es) `yrich_view` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YRICH_VIEW_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YRICH_VIEW_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-types.h>

struct yetty_yclass_object;

struct yetty_ygui_yrich_view;

struct yetty_ygui_yrich_view_ptr_result {
    int ok;
    union {
        struct yetty_ygui_yrich_view *value;
        struct yetty_ycore_error error;
    };
};

struct yetty_yclass_ptr_result yetty_ygui_yrich_view_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_yrich_view_ptr_result yetty_ygui_yrich_view_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_yrich_view_to(struct yetty_ygui_yrich_view *data);

struct yetty_yclass_object_ptr_result yetty_ygui_yrich_view_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_yrich_view_set_document(struct yetty_yclass_object *obj,
                                                                  struct yetty_yrich_document *doc,
                                                                  int own);
struct yetty_ycore_void_result yetty_ygui_yrich_view_invalidate(struct yetty_yclass_object *obj);
struct yetty_yrich_document *yetty_ygui_yrich_view_document(const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_yrich_view_content_size(
    const struct yetty_yclass_object *obj, float *w, float *h);
struct yetty_ycore_void_result yetty_ygui_yrich_view_feed_key(struct yetty_yclass_object *obj,
                                                              uint32_t key,
                                                              struct yetty_yrich_input_mods mods);
struct yetty_ycore_void_result yetty_ygui_yrich_view_feed_text(struct yetty_yclass_object *obj,
                                                               const char *text, size_t len);

#endif
