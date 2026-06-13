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
#include <yetty/yetty/yetty.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>

struct yetty_yshadertoy_figure;

struct yetty_yshadertoy_figure_ptr_result {
    int ok;
    union {
        struct yetty_yshadertoy_figure *value;
        struct yetty_ycore_error error;
    };
};

struct yetty_yclass_ptr_result yetty_yshadertoy_figure_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_yshadertoy_figure_ptr_result yetty_yshadertoy_figure_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yshadertoy_figure_to(struct yetty_yshadertoy_figure *data);

struct yetty_yclass_object_ptr_result yetty_yshadertoy_figure_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yshadertoy_register(void);

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
