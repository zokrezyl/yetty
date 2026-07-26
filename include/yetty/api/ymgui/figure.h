/* GENERATED — do not edit. */
/* Object API for regular class(es) `figure` (implementation module: ymgui).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YMGUI_FIGURE_H
#define YETTY_YCLASSGEN_API_YMGUI_FIGURE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_context;
struct yetty_ycore_rectangle;
struct yetty_yfigure_registry;
struct yetty_ymgui_pipeline;

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMGUI_FACTORY_ARGS
#define YETTY_YCLASSGEN_TYPE_YETTY_YMGUI_FACTORY_ARGS
/* The shared factory args struct is public (hosts embed it by value and
 * hand its address to the registry). `expose` makes codegen re-emit this
 * full definition into the generated figure.h for consumers; this TU has
 * its own copy and the two never share a translation unit. */
struct yetty_ymgui_factory_args {
    const struct yetty_context * context;
    struct yetty_ymgui_pipeline * pipeline;
};
#endif



/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ymgui_figure;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YMGUI_FIGURE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YMGUI_FIGURE_PTR_RESULT
struct yetty_ymgui_figure_ptr_result {
    int ok;
    union {
        struct yetty_ymgui_figure *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ymgui_figure_ptr_result yetty_ymgui_figure_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ymgui_figure_to(struct yetty_ymgui_figure *data);

struct yetty_yclass_object_ptr_result yetty_ymgui_figure_create(struct yetty_yclass_ctx *ctx);



struct yetty_ymgui_figure_ptr_result yetty_ymgui_figure_create_local(struct yetty_ycore_rectangle rect, struct yetty_ymgui_pipeline *pipeline, const struct yetty_context *context);
struct yetty_ymgui_figure_ptr_result yetty_ymgui_figure_from_base(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ymgui_figure_set_frame(struct yetty_yclass_object *obj, const uint8_t *frame_bytes, size_t frame_size);
struct yetty_ycore_void_result yetty_ymgui_figure_set_atlas(struct yetty_yclass_object *obj, const uint8_t *atlas_bytes, size_t atlas_size, uint32_t atlas_w, uint32_t atlas_h);
struct yetty_ycore_void_result yetty_ymgui_register_factory(struct yetty_yfigure_registry *registry, struct yetty_ymgui_factory_args *args);
struct yetty_ycore_void_result yetty_ymgui_factory_args_release(struct yetty_ymgui_factory_args *args);

#ifdef __cplusplus
}
#endif

#endif
