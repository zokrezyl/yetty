/* GENERATED — do not edit. */
/* Public interface for regular class(es) `figure` (module: yfigure).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YFIGURE_FIGURE_H
#define YETTY_YCLASSGEN_YFIGURE_FIGURE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_yfigure_figure_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yfigure_figure;
YETTY_YRESULT_DECLARE(yetty_yfigure_figure_ptr, struct yetty_yfigure_figure *);
struct yetty_yfigure_figure_ptr_result yetty_yfigure_figure_from(struct yetty_yclass_object *obj);
struct rectangle_result yetty_yfigure_figure_rect_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_rect_set(struct yetty_yclass_object *obj,
                                                             struct yetty_ycore_rectangle value);
struct yetty_ycore_int_result yetty_yfigure_figure_z_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_z_set(struct yetty_yclass_object *obj,
                                                          int value);
struct yetty_ycore_int_result yetty_yfigure_figure_hidden_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_hidden_set(struct yetty_yclass_object *obj,
                                                               int value);
struct yetty_ycore_int_result yetty_yfigure_figure_dirty_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_dirty_set(struct yetty_yclass_object *obj,
                                                              int value);
struct yetty_ycore_int_result yetty_yfigure_figure_absolute_coords_get(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_absolute_coords_set(
    struct yetty_yclass_object *obj, int value);

struct yetty_ycore_void_result;

struct yetty_ycore_void_result yetty_yfigure_reset_content(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_set_scroll(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *obj,
                                                        float scroll_x, float scroll_y);
struct yetty_ycore_void_result yetty_yfigure_set_content_size(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              float content_w, float content_h);

struct yetty_yclass_object_ptr_result yetty_yfigure_figure_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yfigure_register(void);

#endif
