/* GENERATED — do not edit. */
/* Public interface for regular class(es) `figure` (module: yshadertoy).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YSHADERTOY_FIGURE_H
#define YETTY_YCLASSGEN_YSHADERTOY_FIGURE_H

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

struct yetty_yclass_ptr_result yetty_yshadertoy_figure_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yshadertoy_figure;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YSHADERTOY_FIGURE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YSHADERTOY_FIGURE_PTR_RESULT
struct yetty_yshadertoy_figure_ptr_result {
    int ok;
    union {
        struct yetty_yshadertoy_figure *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yshadertoy_figure_ptr_result yetty_yshadertoy_figure_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yshadertoy_figure_to(struct yetty_yshadertoy_figure *data);

struct yetty_yclass_object_ptr_result yetty_yshadertoy_figure_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yshadertoy_register(void);

/* Create a yshadertoy figure directly (in-process). `shader_src` is the user
 * WGSL mainImage body; pass NULL to start with the default gradient (set the
 * real source later via set_source). `rect` is absolute pixel space within the
 * parent container. The figure reaches the GPU, event loop (anim timer +
 * repaint nudge) and config through `context`. Returns the yclass object
 * handle — pass it to set_source / as_figure. */
struct yetty_yclass_object_ptr_result yetty_yshadertoy_create(struct yetty_ycore_rectangle rect, const char *shader_src, size_t shader_len, const struct yetty_context *context);
/* Upcast to the figure base handle (a Result — the object may not be of this
 * class). */
struct yetty_yfigure_figure_ptr_result yetty_yshadertoy_as_figure(struct yetty_yclass_object *obj);
/* Replace the shader text and force a recompile on the next render. */
struct yetty_ycore_void_result yetty_yshadertoy_set_source(struct yetty_yclass_object *obj, const char *shader_src, size_t shader_len);
/* Register the yshadertoy factory under YETTY_YFIGURE_KIND_YSHADERTOY.
 * No args bundle: the registry hands the host context to the factory at
 * mint time. Call from yframework's register_figure_factories (the same
 * place ymgui/yrdawn register), once per host registry. */
struct yetty_ycore_void_result yetty_yshadertoy_register_factory(struct yetty_yfigure_registry *registry);

#ifdef __cplusplus
}
#endif

#endif
