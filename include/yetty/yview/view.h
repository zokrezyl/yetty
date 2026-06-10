/* GENERATED — do not edit. */
/* Public interface for regular class(es) `view` (module: yview).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YVIEW_VIEW_H
#define YETTY_YCLASSGEN_YVIEW_VIEW_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_yview_view_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yview_view;
YETTY_YRESULT_DECLARE(yetty_yview_view_ptr, struct yetty_yview_view *);
struct yetty_yview_view_ptr_result yetty_yview_view_from(struct yetty_yclass_object *obj);

struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;

struct yetty_ycore_void_result yetty_yview_configure(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj, int fd,
                                                     uint32_t child_id, uint32_t kind,
                                                     uint32_t bg_color, float min_x, float min_y,
                                                     float max_x, float max_y);
struct yetty_ycore_void_result yetty_yview_set_content(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    const struct yetty_ydraw_drawable_list *content);
struct yetty_ycore_void_result yetty_yview_set_text(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj,
                                                    const char *text, float font_size);
struct yetty_ycore_void_result yetty_yview_set_plot(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj,
                                                    const char *expr, float x_min, float x_max,
                                                    float y_min, float y_max);
struct yetty_ycore_void_result yetty_yview_set_content_size(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj,
                                                            float content_w, float content_h);
struct yetty_ycore_void_result yetty_yview_scroll_to(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj,
                                                     float scroll_x, float scroll_y);
struct yetty_ycore_void_result yetty_yview_scroll_by(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj, float delta_x,
                                                     float delta_y);
struct yetty_ycore_void_result yetty_yview_set_rect(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj, float min_x,
                                                    float min_y, float max_x, float max_y);
struct yetty_ycore_void_result yetty_yview_destroy(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj);

struct yetty_yclass_object_ptr_result yetty_yview_view_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yview_register(void);

#endif
