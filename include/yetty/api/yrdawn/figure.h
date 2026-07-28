/* GENERATED — do not edit. */
/* Object API for regular class(es) `figure` (implementation module: yrdawn).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YRDAWN_FIGURE_H
#define YETTY_YCLASSGEN_API_YRDAWN_FIGURE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yfigure_figure;
struct yetty_yfigure_registry;

typedef struct yetty_ycore_void_result (*yetty_yrdawn_emit_osc_fn)(int, const void *, size_t,
                                                                   void *);
typedef struct yetty_ycore_void_result (*yetty_yrdawn_request_render_fn)(void *);

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YRDAWN_FACTORY_ARGS
#define YETTY_YCLASSGEN_TYPE_YETTY_YRDAWN_FACTORY_ARGS
struct yetty_yrdawn_factory_args {
    const struct yetty_context *context;
    yetty_yrdawn_emit_osc_fn emit_osc_fn;
    void *emit_osc_user;
    yetty_yrdawn_request_render_fn request_render_fn;
    void *request_render_user;
    struct yetty_yrdawn_factory_state *state;
};
#endif

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yrdawn_figure;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YRDAWN_FIGURE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YRDAWN_FIGURE_PTR_RESULT
struct yetty_yrdawn_figure_ptr_result {
    int ok;
    union {
        struct yetty_yrdawn_figure *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yrdawn_figure_ptr_result yetty_yrdawn_figure_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yrdawn_figure_to(struct yetty_yrdawn_figure *data);

struct yetty_yclass_object_ptr_result yetty_yrdawn_figure_create(struct yetty_yclass_ctx *ctx);

struct yetty_yrdawn_figure_ptr_result yetty_yrdawn_figure_from_base(
    struct yetty_yfigure_figure *base);
/* Upcast: object → its figure-base slice. Stable pointer. */
struct yetty_yfigure_figure_ptr_result yetty_yrdawn_figure_as_figure(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrdawn_register_factory(
    struct yetty_yfigure_registry *registry, struct yetty_yrdawn_factory_args *args);
struct yetty_ycore_void_result yetty_yrdawn_factory_args_release(
    struct yetty_yrdawn_factory_args *args);

#ifdef __cplusplus
}
#endif

#endif
