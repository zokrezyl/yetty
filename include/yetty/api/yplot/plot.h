/* GENERATED — do not edit. */
/* Object API for regular class(es) `curve, function, buffer, plot` (implementation module: api_yplot).
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

/* Curve — the shared name/color base of everything a plot draws: a
 * `function` (symbolic body) or a `buffer` (sampled values) IS a curve.
 * The name doubles as the legend label AND the identifier expressions
 * sample a buffer by (`name(x)`). */
struct yetty_yclass_ptr_result yetty_api_yplot_curve_class_get(void);
/* Function — a symbolic curve: the yexpr body, e.g. "sin(x)". */
struct yetty_yclass_ptr_result yetty_api_yplot_function_class_get(void);
/* Buffer — a sampled curve: a named, buffer-backed plot input. `name(x)`
 * inside a Function body samples it; a colored buffer is also drawn as a
 * reference curve. add_buffer lowers it into the DSL. */
struct yetty_yclass_ptr_result yetty_api_yplot_buffer_class_get(void);
/* A plot is also a DRAWABLE (v2 client interface): pack() renders the
 * accumulated DSL into a caller-supplied drawable list as one complex
 * record positioned at the (x, y) origin properties — dlist.add(plot).
 * Everything else (size, ranges, view, flags, title) rides the DSL. */
struct yetty_yclass_ptr_result yetty_api_yplot_plot_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_api_yplot_curve;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_API_YPLOT_CURVE_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_API_YPLOT_CURVE_PTR_RESULT
struct yetty_api_yplot_curve_ptr_result {
    int ok;
    union {
        struct yetty_api_yplot_curve *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_api_yplot_curve_ptr_result yetty_api_yplot_curve_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_api_yplot_curve_to(struct yetty_api_yplot_curve *data);

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

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_api_yplot_buffer;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_API_YPLOT_BUFFER_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_API_YPLOT_BUFFER_PTR_RESULT
struct yetty_api_yplot_buffer_ptr_result {
    int ok;
    union {
        struct yetty_api_yplot_buffer *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_api_yplot_buffer_ptr_result yetty_api_yplot_buffer_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_api_yplot_buffer_to(
    struct yetty_api_yplot_buffer *data);
struct uint32_result yetty_api_yplot_buffer_size_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_api_yplot_buffer_size_set(struct yetty_yclass_object *obj,
                                                               uint32_t value);

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
struct float_result yetty_api_yplot_plot_x_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_api_yplot_plot_x_set(struct yetty_yclass_object *obj,
                                                          float value);
struct float_result yetty_api_yplot_plot_y_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_api_yplot_plot_y_set(struct yetty_yclass_object *obj,
                                                          float value);
struct float_result yetty_api_yplot_plot_width_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_api_yplot_plot_width_set(struct yetty_yclass_object *obj,
                                                              float value);
struct float_result yetty_api_yplot_plot_height_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_api_yplot_plot_height_set(struct yetty_yclass_object *obj,
                                                               float value);

struct yetty_ycore_void_result yetty_api_yplot_set_name(struct yetty_yclass_object *obj,
                                                        const char *name);
struct yetty_ycore_void_result yetty_api_yplot_set_color(struct yetty_yclass_object *obj,
                                                         const char *color);
/* set_body: the function body — the class's primary content. The slot is
 * named distinctly from plot:set_expression because yclass slot names are
 * module-global. */
struct yetty_ycore_void_result yetty_api_yplot_set_body(struct yetty_yclass_object *obj,
                                                        const char *body);
/* set_values: the sample data as an f32 byte buffer. The buffer's PRIMARY
 * content is its NAME (Buffer("env", …)) — the primary@ below marks the
 * inherited set_name slot; this function only anchors the annotation. */
struct yetty_ycore_void_result yetty_api_yplot_set_values(struct yetty_yclass_object *obj,
                                                          struct yetty_ycore_buffer samples);
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
/* add_buffer: lower a Buffer curve into the DSL — name declaration,
 * capacity, inline values (CSV built here, once, in C — no binding
 * re-implements the formatting) and the optional reference-curve color. */
struct yetty_ycore_void_result yetty_api_yplot_add_buffer(struct yetty_yclass_object *obj,
                                                          struct yetty_yclass_object *buffer);
/* set_view: reframe without changing the domain — the DSL's @view=. */
struct yetty_ycore_void_result yetty_api_yplot_set_view(struct yetty_yclass_object *obj,
                                                        float x_min, float x_max, float y_min,
                                                        float y_max);
/* Chrome flags — nonzero disables the element (@plot.grid=0 …); zero
 * re-enables it. Last directive wins, as everywhere in the DSL. */
struct yetty_ycore_void_result yetty_api_yplot_set_nogrid(struct yetty_yclass_object *obj,
                                                          uint32_t disabled);
struct yetty_ycore_void_result yetty_api_yplot_set_noaxes(struct yetty_yclass_object *obj,
                                                          uint32_t disabled);
struct yetty_ycore_void_result yetty_api_yplot_set_nolabels(struct yetty_yclass_object *obj,
                                                            uint32_t disabled);
/* show: render the accumulated DSL and emit it as a DCS envelope on stdout. */
struct yetty_ycore_void_result yetty_api_yplot_show(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_api_yplot_destroy(struct yetty_yclass_object *obj);

struct yetty_yclass_object_ptr_result yetty_api_yplot_curve_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_api_yplot_function_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_api_yplot_buffer_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_api_yplot_plot_create(struct yetty_yclass_ctx *ctx);

#ifdef __cplusplus
}
#endif

#endif
