/* GENERATED — do not edit. */
/* Public interface for regular class(es) `figure` (module: yshadertoy).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YSHADERTOY_FIGURE_H
#define YETTY_YCLASSGEN_YSHADERTOY_FIGURE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_yshadertoy_figure_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yshadertoy_figure;
YETTY_YRESULT_DECLARE(yetty_yshadertoy_figure_ptr, struct yetty_yshadertoy_figure *);
struct yetty_yshadertoy_figure_ptr_result yetty_yshadertoy_figure_from(
    struct yetty_yclass_object *obj);

struct yetty_yclass_object_ptr_result yetty_yshadertoy_figure_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yshadertoy_register(void);

struct yetty_context;
struct yetty_ycore_rectangle;
struct yetty_yfigure_registry;

struct yetty_yclass_object_ptr_result yetty_yshadertoy_create(struct yetty_ycore_rectangle rect,
                                                              const char *shader_src,
                                                              size_t shader_len,
                                                              const struct yetty_context *context);
struct yetty_yfigure_figure_ptr_result yetty_yshadertoy_as_figure(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yshadertoy_set_source(struct yetty_yclass_object *obj,
                                                           const char *shader_src,
                                                           size_t shader_len);
struct yetty_ycore_void_result yetty_yshadertoy_register_factory(
    struct yetty_yfigure_registry *registry);

#endif
