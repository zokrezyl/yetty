/*
 * plot.c — yclass classes `api_yplot:plot` and `api_yplot:function`: the
 * supported public API facade for yplot, from which the FFI / host-language
 * bindings are generated.
 *
 * Design: a Plot is a DSL-string ACCUMULATOR. Every operation — set_expression,
 * set_title, set_size, add_function, … — appends a directive to one growing
 * plot-DSL string. yplot's resolver processes that string left-to-right, so the
 * LAST directive for any property wins (call set_title twice → the second one
 * renders). No precedence rules, no per-property state: the string IS the state.
 * show() renders the accumulated string and emits it as a DCS envelope on stdout
 * (inside a yetty terminal it draws inline). A Function is a small value object
 * (body + optional name/color) that add_function lowers into the same string.
 *
 * These are yclass classes so codegen emits the public headers, dispatch,
 * model.yaml, and the binding surface. The only hand-written file is this
 * annotated .c; plot.gen.c is #included at the foot. Every slot is `local@` —
 * the facade runs in-process. Symbols are `yetty_api_yplot_*`; the facade
 * depends on the implementation (yetty_yplot_*), never the reverse.
 */
#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#include <yetty/ydraw-list/drawable-list.h>
#include <yetty/yplot/resolve.h>
#include <yetty/yplot/yplot.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define API_YPLOT_NAME_MAX 64
#define API_YPLOT_BODY_MAX 256
#define API_YPLOT_COLOR_MAX 16
#define API_YPLOT_BUFFER_VALUES_MAX 1024

static struct yetty_ycore_void_result store_field(char *dest, size_t cap, const char *value,
                                                  const char *what)
{
    if (!value) {
        dest[0] = '\0';
        return YETTY_OK_VOID();
    }
    if (strlen(value) >= cap) {
        return YETTY_ERR(yetty_ycore_void, what);
    }
    snprintf(dest, cap, "%s", value);
    return YETTY_OK_VOID();
}

/* Curve — the shared name/color base of everything a plot draws: a
 * `function` (symbolic body) or a `buffer` (sampled values) IS a curve.
 * The name doubles as the legend label AND the identifier expressions
 * sample a buffer by (`name(x)`). */
struct YETTY_ANNOTATE("class@api_yplot:curve") yetty_api_yplot_curve {
    char name[API_YPLOT_NAME_MAX];   /* legend name; empty = auto-named */
    char color[API_YPLOT_COLOR_MAX]; /* "#RRGGBB"; empty = palette default */
};

YETTY_YRESULT_DECLARE(yetty_api_yplot_curve_ptr, struct yetty_api_yplot_curve *);
#define YETTY_YCLASSGEN_TYPE_YETTY_API_YPLOT_CURVE_PTR_RESULT

struct yetty_yclass_ptr_result yetty_api_yplot_curve_class_get(void);

static struct yetty_yclass_void_ptr_result curve_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_curve_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "curve_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "curve_from_obj: object_data");
    return slice_r;
}

YETTY_ANNOTATE("virtual@api_yplot:curve:set_name")
YETTY_ANNOTATE("local@api_yplot:set_name")
static struct yetty_ycore_void_result curve_set_name(struct yetty_yclass_object *obj,
                                                     const char *name)
{
    struct yetty_yclass_void_ptr_result curve_r = curve_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, curve_r, "api_yplot curve set_name: object");
    struct yetty_api_yplot_curve *curve = (struct yetty_api_yplot_curve *)curve_r.value;
    return store_field(curve->name, sizeof(curve->name), name, "api_yplot curve: name too long");
}

YETTY_ANNOTATE("virtual@api_yplot:curve:set_color")
YETTY_ANNOTATE("local@api_yplot:set_color")
static struct yetty_ycore_void_result curve_set_color(struct yetty_yclass_object *obj,
                                                      const char *color)
{
    struct yetty_yclass_void_ptr_result curve_r = curve_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, curve_r, "api_yplot curve set_color: object");
    struct yetty_api_yplot_curve *curve = (struct yetty_api_yplot_curve *)curve_r.value;
    return store_field(curve->color, sizeof(curve->color), color,
                       "api_yplot curve: color too long");
}

/* Function — a symbolic curve: the yexpr body, e.g. "sin(x)". */
struct YETTY_ANNOTATE("class@api_yplot:function") YETTY_ANNOTATE("parent@api_yplot:curve")
    yetty_api_yplot_function {
    char body[API_YPLOT_BODY_MAX]; /* the expression, e.g. "sin(x)" */
};

YETTY_YRESULT_DECLARE(yetty_api_yplot_function_ptr, struct yetty_api_yplot_function *);
#define YETTY_YCLASSGEN_TYPE_YETTY_API_YPLOT_FUNCTION_PTR_RESULT

struct yetty_yclass_ptr_result yetty_api_yplot_function_class_get(void);

static struct yetty_yclass_void_ptr_result function_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_function_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "function_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "function_from_obj: object_data");
    return slice_r;
}

/* set_body: the function body — the class's primary content. The slot is
 * named distinctly from plot:set_expression because yclass slot names are
 * module-global. */
YETTY_ANNOTATE("virtual@api_yplot:function:set_body")
YETTY_ANNOTATE("primary@api_yplot:set_body")
YETTY_ANNOTATE("local@api_yplot:set_body")
static struct yetty_ycore_void_result function_set_body(struct yetty_yclass_object *obj,
                                                        const char *body)
{
    struct yetty_yclass_void_ptr_result function_r = function_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, function_r, "api_yplot function set_body: object");
    struct yetty_api_yplot_function *fn = (struct yetty_api_yplot_function *)function_r.value;
    return store_field(fn->body, sizeof(fn->body), body, "api_yplot function: body too long");
}

/* Buffer — a sampled curve: a named, buffer-backed plot input. `name(x)`
 * inside a Function body samples it; a colored buffer is also drawn as a
 * reference curve. add_buffer lowers it into the DSL. */
struct YETTY_ANNOTATE("class@api_yplot:buffer") YETTY_ANNOTATE("parent@api_yplot:curve")
    yetty_api_yplot_buffer {
    YETTY_ANNOTATE("property") uint32_t size; /* capacity; 0 = value count */
    uint32_t value_count;
    float values[API_YPLOT_BUFFER_VALUES_MAX];
};

YETTY_YRESULT_DECLARE(yetty_api_yplot_buffer_ptr, struct yetty_api_yplot_buffer *);
#define YETTY_YCLASSGEN_TYPE_YETTY_API_YPLOT_BUFFER_PTR_RESULT

struct yetty_yclass_ptr_result yetty_api_yplot_buffer_class_get(void);

static struct yetty_yclass_void_ptr_result buffer_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_buffer_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "buffer_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "buffer_from_obj: object_data");
    return slice_r;
}

/* set_values: the sample data as an f32 byte buffer. The buffer's PRIMARY
 * content is its NAME (Buffer("env", …)) — the primary@ below marks the
 * inherited set_name slot; this function only anchors the annotation. */
YETTY_ANNOTATE("virtual@api_yplot:buffer:set_values")
YETTY_ANNOTATE("primary@api_yplot:set_name")
YETTY_ANNOTATE("local@api_yplot:set_values")
static struct yetty_ycore_void_result buffer_set_values(struct yetty_yclass_object *obj,
                                                        struct yetty_ycore_buffer samples)
{
    struct yetty_yclass_void_ptr_result buffer_r = buffer_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, buffer_r, "api_yplot buffer set_values: object");
    struct yetty_api_yplot_buffer *buffer = (struct yetty_api_yplot_buffer *)buffer_r.value;
    if (!samples.data || samples.size == 0) {
        buffer->value_count = 0;
        return YETTY_OK_VOID();
    }
    if (samples.size % sizeof(float) != 0) {
        return YETTY_ERR(yetty_ycore_void, "api_yplot buffer set_values: not an f32 array");
    }
    size_t count = samples.size / sizeof(float);
    if (count > API_YPLOT_BUFFER_VALUES_MAX) {
        return YETTY_ERR(yetty_ycore_void, "api_yplot buffer set_values: too many samples");
    }
    memcpy(buffer->values, samples.data, samples.size);
    buffer->value_count = (uint32_t)count;
    return YETTY_OK_VOID();
}

/* A plot is also a DRAWABLE (v2 client interface): pack() renders the
 * accumulated DSL into a caller-supplied drawable list as one complex
 * record positioned at the (x, y) origin properties — dlist.add(plot).
 * Everything else (size, ranges, view, flags, title) rides the DSL. */
struct YETTY_ANNOTATE("class@api_yplot:plot") YETTY_ANNOTATE("parent@ydrawlist2:drawable")
    yetty_api_yplot_plot {
    YETTY_ANNOTATE("property") float x;
    YETTY_ANNOTATE("property") float y;
    /* Drawable bounds for pack(); 0 = the 400x200 default. show()'s bounds
     * come from the DSL (@plot.size) as before. */
    YETTY_ANNOTATE("property") float width;
    YETTY_ANNOTATE("property") float height;
    /* Stacking depth (z-order), uniform with every other drawable's `layer`.
     * Client-side only — a complex carries no z in its wire record, so pack()
     * brackets the whole figure in a paint-z scope instead. */
    YETTY_ANNOTATE("property") int32_t layer;
    /* Addressable id (0 = anonymous). A nonzero id makes the complex ITSELF
     * the addressable node at (enclosing path . id) — later CMD_UPDATE at
     * that path streams data into this exact plot. */
    YETTY_ANNOTATE("property") uint32_t id;
    /* Self-owned chrome (0 = off): pack() brackets the label/title/legend
     * prims in a GROUP with this id and the record grows a chrome-state
     * tail, so the receiver re-renders the chrome LOCALLY on a geometry /
     * range op — resize never re-ships the record or its samples. */
    YETTY_ANNOTATE("property") uint32_t chrome_group;
    char *source; /* accumulating plot-DSL string (owned), NUL-terminated */
    size_t length;
    size_t capacity;
};

/* Result wrappers declared here (the TU does not include its own generated
 * header); plot.gen.c defines the *_from() accessors returning them. */
YETTY_YRESULT_DECLARE(yetty_api_yplot_plot_ptr, struct yetty_api_yplot_plot *);
#define YETTY_YCLASSGEN_TYPE_YETTY_API_YPLOT_PLOT_PTR_RESULT

struct yetty_yclass_ptr_result yetty_api_yplot_plot_class_get(void);
struct yetty_api_yplot_plot_ptr_result yetty_api_yplot_plot_from(struct yetty_yclass_object *obj);

static struct yetty_yclass_void_ptr_result plot_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_plot_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "plot_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "plot_from_obj: object_data");
    return slice_r;
}

/* Append `text` to the plot's DSL string, prefixed with "; " unless the string
 * is empty. Grows the buffer as needed. */
static struct yetty_ycore_void_result plot_append(struct yetty_api_yplot_plot *plot,
                                                  const char *text)
{
    size_t text_len = strlen(text);
    size_t separator = plot->length > 0 ? 2 : 0; /* "; " */
    size_t needed = plot->length + separator + text_len + 1;
    if (needed > plot->capacity) {
        size_t new_cap = plot->capacity ? plot->capacity * 2 : 256;
        while (new_cap < needed) {
            new_cap *= 2;
        }
        char *grown = realloc(plot->source, new_cap);
        if (!grown) {
            return YETTY_ERR(yetty_ycore_void, "api_yplot: out of memory");
        }
        plot->source = grown;
        plot->capacity = new_cap;
    }
    if (separator) {
        memcpy(plot->source + plot->length, "; ", 2);
        plot->length += 2;
    }
    memcpy(plot->source + plot->length, text, text_len + 1);
    plot->length += text_len;
    return YETTY_OK_VOID();
}

/* Append a printf-formatted directive. */
static struct yetty_ycore_void_result plot_appendf(struct yetty_api_yplot_plot *plot,
                                                   const char *fmt, ...)
{
    char buffer[API_YPLOT_BODY_MAX + API_YPLOT_NAME_MAX + 32];
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof(buffer)) {
        return YETTY_ERR(yetty_ycore_void, "api_yplot: directive too long");
    }
    return plot_append(plot, buffer);
}

/*=============================================================================
 * plot slots
 *===========================================================================*/

/* set_expression: append a raw plot-DSL fragment (curves, per-curve colors, and
 * any figure/axis attributes such as @plot.title / @x.scale). */
YETTY_ANNOTATE("virtual@api_yplot:plot:set_expression")
YETTY_ANNOTATE("primary@api_yplot:set_expression")
YETTY_ANNOTATE("local@api_yplot:set_expression")
static struct yetty_ycore_void_result plot_set_expression(struct yetty_yclass_object *obj,
                                                          const char *source)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot set_expression: object");
    if (!source) {
        return YETTY_ERR(yetty_ycore_void, "api_yplot set_expression: NULL source");
    }
    return plot_append((struct yetty_api_yplot_plot *)plot_r.value, source);
}

/* add_function: append a Function value object as a named curve (plus its color
 * if set). */
YETTY_ANNOTATE("virtual@api_yplot:plot:add_function")
YETTY_ANNOTATE("local@api_yplot:add_function")
static struct yetty_ycore_void_result plot_add_function(struct yetty_yclass_object *obj,
                                                        struct yetty_yclass_object *function)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot add_function: plot object");
    struct yetty_api_yplot_plot *plot = (struct yetty_api_yplot_plot *)plot_r.value;
    if (!function) {
        return YETTY_ERR(yetty_ycore_void, "api_yplot add_function: NULL function");
    }
    struct yetty_yclass_void_ptr_result function_r = function_from_obj(function);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, function_r, "api_yplot add_function: function object");
    struct yetty_api_yplot_function *fn = (struct yetty_api_yplot_function *)function_r.value;
    struct yetty_yclass_void_ptr_result curve_r = curve_from_obj(function);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, curve_r, "api_yplot add_function: curve slice");
    struct yetty_api_yplot_curve *curve = (struct yetty_api_yplot_curve *)curve_r.value;
    if (!fn->body[0]) {
        return YETTY_ERR(yetty_ycore_void, "api_yplot add_function: function has no body");
    }

    struct yetty_ycore_void_result append_res;
    if (curve->name[0]) {
        append_res = plot_appendf(plot, "%s=%s", curve->name, fn->body);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "api_yplot add_function: def");
        if (curve->color[0]) {
            append_res = plot_appendf(plot, "@%s.color=%s", curve->name, curve->color);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "api_yplot add_function: color");
        }
    } else {
        /* Unnamed: a bare expression the DSL auto-names (no color channel). */
        append_res = plot_append(plot, fn->body);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "api_yplot add_function: bare");
    }
    return YETTY_OK_VOID();
}

/* Figure/axis setters — each appends the matching DSL directive; last wins. */
YETTY_ANNOTATE("virtual@api_yplot:plot:set_title")
YETTY_ANNOTATE("local@api_yplot:set_title")
static struct yetty_ycore_void_result plot_set_title(struct yetty_yclass_object *obj,
                                                     const char *title)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot set_title: object");
    return plot_appendf((struct yetty_api_yplot_plot *)plot_r.value, "@plot.title=\"%s\"",
                        title ? title : "");
}

YETTY_ANNOTATE("virtual@api_yplot:plot:set_x_label")
YETTY_ANNOTATE("local@api_yplot:set_x_label")
static struct yetty_ycore_void_result plot_set_x_label(struct yetty_yclass_object *obj,
                                                       const char *label)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot set_x_label: object");
    return plot_appendf((struct yetty_api_yplot_plot *)plot_r.value, "@x.label=\"%s\"",
                        label ? label : "");
}

YETTY_ANNOTATE("virtual@api_yplot:plot:set_y_label")
YETTY_ANNOTATE("local@api_yplot:set_y_label")
static struct yetty_ycore_void_result plot_set_y_label(struct yetty_yclass_object *obj,
                                                       const char *label)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot set_y_label: object");
    return plot_appendf((struct yetty_api_yplot_plot *)plot_r.value, "@y.label=\"%s\"",
                        label ? label : "");
}

YETTY_ANNOTATE("virtual@api_yplot:plot:set_size")
YETTY_ANNOTATE("local@api_yplot:set_size")
static struct yetty_ycore_void_result plot_set_size(struct yetty_yclass_object *obj, float width,
                                                    float height)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot set_size: object");
    return plot_appendf((struct yetty_api_yplot_plot *)plot_r.value, "@plot.size=%g,%g",
                        (double)width, (double)height);
}

YETTY_ANNOTATE("virtual@api_yplot:plot:set_x_range")
YETTY_ANNOTATE("local@api_yplot:set_x_range")
static struct yetty_ycore_void_result plot_set_x_range(struct yetty_yclass_object *obj, float min,
                                                       float max)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot set_x_range: object");
    return plot_appendf((struct yetty_api_yplot_plot *)plot_r.value, "x=%g..%g", (double)min,
                        (double)max);
}

YETTY_ANNOTATE("virtual@api_yplot:plot:set_y_range")
YETTY_ANNOTATE("local@api_yplot:set_y_range")
static struct yetty_ycore_void_result plot_set_y_range(struct yetty_yclass_object *obj, float min,
                                                       float max)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot set_y_range: object");
    return plot_appendf((struct yetty_api_yplot_plot *)plot_r.value, "y=%g..%g", (double)min,
                        (double)max);
}

/* add_buffer: lower a Buffer curve into the DSL — name declaration,
 * capacity, inline values (CSV built here, once, in C — no binding
 * re-implements the formatting) and the optional reference-curve color. */
YETTY_ANNOTATE("virtual@api_yplot:plot:add_buffer")
YETTY_ANNOTATE("local@api_yplot:add_buffer")
static struct yetty_ycore_void_result plot_add_buffer(struct yetty_yclass_object *obj,
                                                      struct yetty_yclass_object *buffer)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot add_buffer: plot object");
    struct yetty_api_yplot_plot *plot = (struct yetty_api_yplot_plot *)plot_r.value;
    if (!buffer) {
        return YETTY_ERR(yetty_ycore_void, "api_yplot add_buffer: NULL buffer");
    }
    struct yetty_yclass_void_ptr_result buffer_r = buffer_from_obj(buffer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, buffer_r, "api_yplot add_buffer: buffer object");
    struct yetty_api_yplot_buffer *data = (struct yetty_api_yplot_buffer *)buffer_r.value;
    struct yetty_yclass_void_ptr_result curve_r = curve_from_obj(buffer);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, curve_r, "api_yplot add_buffer: curve slice");
    struct yetty_api_yplot_curve *curve = (struct yetty_api_yplot_curve *)curve_r.value;
    if (!curve->name[0]) {
        return YETTY_ERR(yetty_ycore_void, "api_yplot add_buffer: buffer has no name");
    }
    struct yetty_ycore_void_result append_res = plot_appendf(plot, "%s=buffer", curve->name);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "api_yplot add_buffer: decl");
    uint32_t capacity = data->size ? data->size : data->value_count;
    if (capacity) {
        append_res = plot_appendf(plot, "@%s.size=%u", curve->name, capacity);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "api_yplot add_buffer: size");
    }
    if (data->value_count) {
        /* "@name.values=v1,v2,…" — worst case ~14 chars per float. */
        size_t csv_cap = strlen(curve->name) + 16 + (size_t)data->value_count * 14;
        char *csv = malloc(csv_cap);
        if (!csv) {
            return YETTY_ERR(yetty_ycore_void, "api_yplot add_buffer: alloc");
        }
        int written = snprintf(csv, csv_cap, "@%s.values=", curve->name);
        for (uint32_t i = 0; i < data->value_count; i++) {
            written += snprintf(csv + written, csv_cap - (size_t)written, "%s%g", i ? "," : "",
                                (double)data->values[i]);
        }
        append_res = plot_append(plot, csv);
        free(csv);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "api_yplot add_buffer: values");
    }
    if (curve->color[0]) {
        append_res = plot_appendf(plot, "@%s.color=%s", curve->name, curve->color);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "api_yplot add_buffer: color");
    }
    return YETTY_OK_VOID();
}

/* set_view: reframe without changing the domain — the DSL's @view=. */
YETTY_ANNOTATE("virtual@api_yplot:plot:set_view")
YETTY_ANNOTATE("local@api_yplot:set_view")
static struct yetty_ycore_void_result plot_set_view(struct yetty_yclass_object *obj, float x_min,
                                                    float x_max, float y_min, float y_max)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot set_view: object");
    return plot_appendf((struct yetty_api_yplot_plot *)plot_r.value, "@view=%g..%g,%g..%g",
                        (double)x_min, (double)x_max, (double)y_min, (double)y_max);
}

/* Chrome flags — nonzero disables the element (@plot.grid=0 …); zero
 * re-enables it. Last directive wins, as everywhere in the DSL. */
YETTY_ANNOTATE("virtual@api_yplot:plot:set_nogrid")
YETTY_ANNOTATE("local@api_yplot:set_nogrid")
static struct yetty_ycore_void_result plot_set_nogrid(struct yetty_yclass_object *obj,
                                                      uint32_t disabled)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot set_nogrid: object");
    return plot_appendf((struct yetty_api_yplot_plot *)plot_r.value, "@plot.grid=%u",
                        disabled ? 0u : 1u);
}

YETTY_ANNOTATE("virtual@api_yplot:plot:set_noaxes")
YETTY_ANNOTATE("local@api_yplot:set_noaxes")
static struct yetty_ycore_void_result plot_set_noaxes(struct yetty_yclass_object *obj,
                                                      uint32_t disabled)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot set_noaxes: object");
    return plot_appendf((struct yetty_api_yplot_plot *)plot_r.value, "@plot.axes=%u",
                        disabled ? 0u : 1u);
}

YETTY_ANNOTATE("virtual@api_yplot:plot:set_nolabels")
YETTY_ANNOTATE("local@api_yplot:set_nolabels")
static struct yetty_ycore_void_result plot_set_nolabels(struct yetty_yclass_object *obj,
                                                        uint32_t disabled)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot set_nolabels: object");
    return plot_appendf((struct yetty_api_yplot_plot *)plot_r.value, "@plot.labels=%u",
                        disabled ? 0u : 1u);
}

/* pack: render the accumulated DSL INTO the caller's drawable list at the
 * (x, y) origin — the v2 drawable contract. Defaults mirror yecho's plot
 * block; every DSL directive ("expression attributes win") overrides them. */
YETTY_ANNOTATE("override@ydrawlist2:drawable:pack")
static struct yetty_ycore_void_result plot_pack(struct yetty_yclass_object *obj,
                                                struct yetty_ydraw_drawable_list *list)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot pack: object");
    struct yetty_api_yplot_plot *plot = (struct yetty_api_yplot_plot *)plot_r.value;
    const char *source = plot->source ? plot->source : "";
    struct yetty_yplot_render_config config = {
        .bounds_w = plot->width > 0.0f ? plot->width : 400.0f,
        .bounds_h = plot->height > 0.0f ? plot->height : 200.0f,
        .x_min = -3.14159f,
        .x_max = 3.14159f,
        .y_min = -1.5f,
        .y_max = 1.5f,
        .flags = YETTY_YPLOT_FLAG_GRID | YETTY_YPLOT_FLAG_AXES | YETTY_YPLOT_FLAG_LABELS,
        .chrome_group_id = plot->chrome_group,
    };
    /* Stack the WHOLE figure (complex record + its axis-label / legend
     * prims) at `layer` by bracketing the emit in a paint-z scope. A
     * complex carries no z in its wire record; layer 0 is the default
     * plane and needs no bracket. */
    if (plot->layer != 0) {
        struct yetty_ycore_void_result open_res =
            yetty_ydraw_drawable_list_add_cmd_paint_z(list, plot->layer);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, open_res, "api_yplot pack: paint_z open");
    }
    /* A nonzero id makes the complex ITSELF the addressable node (its own
     * id, no wrapper group): CMD_NODE_ID latches onto the emitted complex
     * record, so CMD_UPDATE(id, …) streams data here. */
    if (plot->id != 0) {
        struct yetty_ycore_void_result node_id_res =
            yetty_ydraw_drawable_list_add_cmd_node_id(list, plot->id);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, node_id_res, "api_yplot pack: node_id");
    }
    struct yetty_ycore_void_result emit_r = yetty_yplot_emit_expression(
        source, strlen(source), NULL, 0, &config, list, plot->x, plot->y, NULL, NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_r, "api_yplot pack: emit_expression");
    if (plot->layer != 0) {
        struct yetty_ycore_void_result close_res =
            yetty_ydraw_drawable_list_add_cmd_paint_z_end(list);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, close_res, "api_yplot pack: paint_z close");
    }
    return YETTY_OK_VOID();
}

/* show: render the accumulated DSL and emit it as a DCS envelope on stdout. */
YETTY_ANNOTATE("virtual@api_yplot:plot:show")
YETTY_ANNOTATE("local@api_yplot:show")
static struct yetty_ycore_void_result plot_show(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, plot_r, "api_yplot show: object");
    struct yetty_api_yplot_plot *plot = (struct yetty_api_yplot_plot *)plot_r.value;

    const char *source = plot->source ? plot->source : "";
    struct yetty_ydraw_drawable_list_result render_r =
        yetty_yplot_render(source, strlen(source), NULL);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_r, "api_yplot show: render");
    struct yetty_ycore_size_result emit_r = yetty_yplot_dcs_bin_emit(render_r.value, stdout);
    yetty_ydraw_drawable_list_destroy(render_r.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, emit_r, "api_yplot show: emit");
    /* show() means "draw it now": flush so the envelope reaches the terminal
     * immediately rather than sitting in the stdio buffer until exit. */
    if (fflush(stdout) != 0) {
        return YETTY_ERR(yetty_ycore_void, "api_yplot show: fflush");
    }
    return YETTY_OK_VOID();
}

YETTY_ANNOTATE("virtual@api_yplot:plot:destroy")
YETTY_ANNOTATE("local@api_yplot:destroy")
static struct yetty_ycore_void_result plot_destroy(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_void_ptr_result plot_r = plot_from_obj(obj);
    if (YETTY_IS_OK(plot_r)) {
        struct yetty_api_yplot_plot *plot = (struct yetty_api_yplot_plot *)plot_r.value;
        free(plot->source);
        plot->source = NULL;
    }
    struct yetty_ycore_void_result free_r = yetty_yclass_object_free(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, free_r, "api_yplot destroy: object_free");
    return YETTY_OK_VOID();
}

#include "yetty/gen/impl/api_yplot/plot.c"
