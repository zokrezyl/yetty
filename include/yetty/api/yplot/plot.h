/* GENERATED — do not edit. */
/* Object API for regular class(es) `plot, function` (implementation module: api_yplot).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YPLOT_PLOT_H
#define YETTY_YCLASSGEN_API_YPLOT_PLOT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_api_yplot_plot;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_API_YPLOT_PLOT_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_API_YPLOT_PLOT_PTR_RESULT
struct yetty_api_yplot_plot_ptr_result {
    int ok;
    union {
        struct yetty_api_yplot_plot *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_api_yplot_plot_ptr_result yetty_api_yplot_plot_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_api_yplot_plot_to(struct yetty_api_yplot_plot *data);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_api_yplot_function;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_API_YPLOT_FUNCTION_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_API_YPLOT_FUNCTION_PTR_RESULT
struct yetty_api_yplot_function_ptr_result {
    int ok;
    union {
        struct yetty_api_yplot_function *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_api_yplot_function_ptr_result yetty_api_yplot_function_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_api_yplot_function_to(
    struct yetty_api_yplot_function *data);

/* set_expression: append a raw plot-DSL fragment (curves, per-curve colors, and
 * any figure/axis attributes such as @plot.title / @x.scale). */
struct yetty_ycore_void_result yetty_api_yplot_set_expression(struct yetty_yclass_object *obj,
                                                              const char *source);
/* add_function: append a Function value object as a named curve (plus its color
 * if set). */
struct yetty_ycore_void_result yetty_api_yplot_add_function(struct yetty_yclass_object *obj,
                                                            struct yetty_yclass_object *function);
/* Figure/axis setters — each appends the matching DSL directive; last wins. */
struct yetty_ycore_void_result yetty_api_yplot_set_title(struct yetty_yclass_object *obj,
                                                         const char *title);
struct yetty_ycore_void_result yetty_api_yplot_set_x_label(struct yetty_yclass_object *obj,
                                                           const char *label);
struct yetty_ycore_void_result yetty_api_yplot_set_y_label(struct yetty_yclass_object *obj,
                                                           const char *label);
struct yetty_ycore_void_result yetty_api_yplot_set_size(struct yetty_yclass_object *obj,
                                                        float width, float height);
struct yetty_ycore_void_result yetty_api_yplot_set_x_range(struct yetty_yclass_object *obj,
                                                           float min, float max);
struct yetty_ycore_void_result yetty_api_yplot_set_y_range(struct yetty_yclass_object *obj,
                                                           float min, float max);
/* show: render the accumulated DSL and emit it as a DCS envelope on stdout. */
struct yetty_ycore_void_result yetty_api_yplot_show(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_api_yplot_destroy(struct yetty_yclass_object *obj);
/* set_body: the function body, e.g. "sin(x)". (The generated create() maps its
 * positional argument to this, so Function.create("sin(x)") works.) The slot is
 * named distinctly from plot:set_expression because yclass slot names are
 * module-global. */
struct yetty_ycore_void_result yetty_api_yplot_set_body(struct yetty_yclass_object *obj,
                                                        const char *body);
struct yetty_ycore_void_result yetty_api_yplot_set_name(struct yetty_yclass_object *obj,
                                                        const char *name);
struct yetty_ycore_void_result yetty_api_yplot_set_color(struct yetty_yclass_object *obj,
                                                         const char *color);

struct yetty_yclass_object_ptr_result yetty_api_yplot_plot_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_api_yplot_function_create(struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
