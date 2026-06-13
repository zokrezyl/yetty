/* GENERATED — do not edit. */
/* Public interface for regular class(es) `figure` (module: yrdawn).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YRDAWN_FIGURE_H
#define YETTY_YCLASSGEN_YRDAWN_FIGURE_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yfigure/registry.h>

struct yetty_yrdawn_factory_state;

struct yetty_yrdawn_figure;

struct yetty_yrdawn_figure_ptr_result {
    int ok;
    union {
        struct yetty_yrdawn_figure *value;
        struct yetty_ycore_error error;
    };
};

typedef struct yetty_ycore_void_result (*yetty_yrdawn_emit_osc_fn)(int, const void *, size_t,
                                                                   void *);

typedef struct yetty_ycore_void_result (*yetty_yrdawn_request_render_fn)(void *);

struct yetty_yrdawn_factory_args {
    const struct yetty_context *context;
    yetty_yrdawn_emit_osc_fn emit_osc_fn;
    void *emit_osc_user;
    yetty_yrdawn_request_render_fn request_render_fn;
    void *request_render_user;
    struct yetty_yrdawn_factory_state *state;
};

struct yetty_yclass_ptr_result yetty_yrdawn_figure_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_yrdawn_figure_ptr_result yetty_yrdawn_figure_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yrdawn_figure_to(struct yetty_yrdawn_figure *data);

struct yetty_yclass_object_ptr_result yetty_yrdawn_figure_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yrdawn_register(void);

struct yetty_yrdawn_figure_ptr_result yetty_yrdawn_figure_from_base(
    struct yetty_yfigure_figure *base);
struct yetty_yfigure_figure *yetty_yrdawn_figure_as_figure(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yrdawn_register_factory(
    struct yetty_yfigure_registry *registry, struct yetty_yrdawn_factory_args *args);
struct yetty_ycore_void_result yetty_yrdawn_factory_args_release(
    struct yetty_yrdawn_factory_args *args);

#endif
