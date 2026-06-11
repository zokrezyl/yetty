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

struct yetty_yclass_ptr_result yetty_ygui_yrich_view_class_get(void);

struct yetty_ygui_object;
struct yetty_ygui_yrich_view;
YETTY_YRESULT_DECLARE(yetty_ygui_yrich_view_data_ptr, struct yetty_ygui_yrich_view *);
struct yetty_ygui_yrich_view_data_ptr_result yetty_ygui_yrich_view_data(
    struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_yrich_view_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_object;
struct yetty_yrich_document;
struct yetty_yrich_input_mods;

/* Header-destined: the exposed prototypes below take yrich document/key
 * types, so the generated header must pull these in. */
#include <yetty/yrich/yrich-document.h>
#include <yetty/yrich/yrich-types.h>
struct yetty_ycore_void_result yetty_ygui_yrich_view_set_document(struct yetty_ygui_object *obj,
                                                                  struct yetty_yrich_document *doc,
                                                                  int own);
struct yetty_ycore_void_result yetty_ygui_yrich_view_invalidate(struct yetty_ygui_object *obj);
struct yetty_yrich_document *yetty_ygui_yrich_view_document(const struct yetty_ygui_object *obj);
struct yetty_ycore_void_result yetty_ygui_yrich_view_content_size(
    const struct yetty_ygui_object *obj, float *w, float *h);
struct yetty_ycore_void_result yetty_ygui_yrich_view_feed_key(struct yetty_ygui_object *obj,
                                                              uint32_t key,
                                                              struct yetty_yrich_input_mods mods);
struct yetty_ycore_void_result yetty_ygui_yrich_view_feed_text(struct yetty_ygui_object *obj,
                                                               const char *text, size_t len);

#endif
