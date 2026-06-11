/* GENERATED — do not edit. */
/* Public interface for regular class(es) `figure` (module: ymgui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YMGUI_FIGURE_H
#define YETTY_YCLASSGEN_YMGUI_FIGURE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ymgui_figure_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymgui_figure;
YETTY_YRESULT_DECLARE(yetty_ymgui_figure_ptr, struct yetty_ymgui_figure *);
struct yetty_ymgui_figure_ptr_result yetty_ymgui_figure_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ymgui_figure_to(struct yetty_ymgui_figure *data);

struct yetty_yclass_object_ptr_result yetty_ymgui_figure_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ymgui_register(void);

struct yetty_ycore_rectangle;
struct yetty_yfigure_registry;

struct yetty_ymgui_factory_args {
    const struct yetty_context *context;
    struct yetty_ymgui_pipeline *pipeline;
};
struct yetty_ymgui_figure_ptr_result yetty_ymgui_figure_create_local(
    struct yetty_ycore_rectangle rect, struct yetty_ymgui_pipeline *pipeline,
    const struct yetty_context *context);
struct yetty_ymgui_figure_ptr_result yetty_ymgui_figure_from_base(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymgui_figure_set_frame(struct yetty_yclass_object *obj,
                                                            const uint8_t *frame_bytes,
                                                            size_t frame_size);
struct yetty_ycore_void_result yetty_ymgui_figure_set_atlas(struct yetty_yclass_object *obj,
                                                            const uint8_t *atlas_bytes,
                                                            size_t atlas_size, uint32_t atlas_w,
                                                            uint32_t atlas_h);
struct yetty_ycore_void_result yetty_ymgui_register_factory(struct yetty_yfigure_registry *registry,
                                                            struct yetty_ymgui_factory_args *args);
struct yetty_ycore_void_result yetty_ymgui_factory_args_release(
    struct yetty_ymgui_factory_args *args);

#endif
