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

#include <yetty/yplot/yplot.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define API_YPLOT_NAME_MAX 64
#define API_YPLOT_BODY_MAX 256
#define API_YPLOT_COLOR_MAX 16

struct YETTY_ANNOTATE("class@api_yplot:plot") yetty_api_yplot_plot {
    char *source; /* accumulating plot-DSL string (owned), NUL-terminated */
    size_t length;
    size_t capacity;
};

struct YETTY_ANNOTATE("class@api_yplot:function") yetty_api_yplot_function {
    char name[API_YPLOT_NAME_MAX];   /* legend name; empty = auto-named */
    char body[API_YPLOT_BODY_MAX];   /* the expression, e.g. "sin(x)" */
    char color[API_YPLOT_COLOR_MAX]; /* "#RRGGBB"; empty = palette default */
};

/* Result wrappers declared here (the TU does not include its own generated
 * header); plot.gen.c defines the *_from() accessors returning them. */
YETTY_YRESULT_DECLARE(yetty_api_yplot_plot_ptr, struct yetty_api_yplot_plot *);
YETTY_YRESULT_DECLARE(yetty_api_yplot_function_ptr, struct yetty_api_yplot_function *);

struct yetty_yclass_ptr_result yetty_api_yplot_plot_class_get(void);
struct yetty_api_yplot_plot_ptr_result yetty_api_yplot_plot_from(struct yetty_yclass_object *obj);
struct yetty_yclass_ptr_result yetty_api_yplot_function_class_get(void);
struct yetty_api_yplot_function_ptr_result yetty_api_yplot_function_from(
    struct yetty_yclass_object *obj);

static struct yetty_yclass_void_ptr_result plot_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_plot_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "plot_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "plot_from_obj: object_data");
    return slice_r;
}

static struct yetty_yclass_void_ptr_result function_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_api_yplot_function_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_r, "function_from_obj: class_get");
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, slice_r, "function_from_obj: object_data");
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

/*=============================================================================
 * plot slots
 *===========================================================================*/

/* set_expression: append a raw plot-DSL fragment (curves, per-curve colors, and
 * any figure/axis attributes such as @plot.title / @x.scale). */
YETTY_ANNOTATE("virtual@api_yplot:plot:set_expression")
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
    if (!fn->body[0]) {
        return YETTY_ERR(yetty_ycore_void, "api_yplot add_function: function has no body");
    }

    struct yetty_ycore_void_result append_res;
    if (fn->name[0]) {
        append_res = plot_appendf(plot, "%s=%s", fn->name, fn->body);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, append_res, "api_yplot add_function: def");
        if (fn->color[0]) {
            append_res = plot_appendf(plot, "@%s.color=%s", fn->name, fn->color);
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

/*=============================================================================
 * function slots
 *===========================================================================*/

/* set_body: the function body, e.g. "sin(x)". (The generated create() maps its
 * positional argument to this, so Function.create("sin(x)") works.) The slot is
 * named distinctly from plot:set_expression because yclass slot names are
 * module-global. */
YETTY_ANNOTATE("virtual@api_yplot:function:set_body")
YETTY_ANNOTATE("local@api_yplot:set_body")
static struct yetty_ycore_void_result function_set_body(struct yetty_yclass_object *obj,
                                                        const char *body)
{
    struct yetty_yclass_void_ptr_result function_r = function_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, function_r, "api_yplot function set_body: object");
    struct yetty_api_yplot_function *fn = (struct yetty_api_yplot_function *)function_r.value;
    return store_field(fn->body, sizeof(fn->body), body, "api_yplot function: body too long");
}

YETTY_ANNOTATE("virtual@api_yplot:function:set_name")
YETTY_ANNOTATE("local@api_yplot:set_name")
static struct yetty_ycore_void_result function_set_name(struct yetty_yclass_object *obj,
                                                        const char *name)
{
    struct yetty_yclass_void_ptr_result function_r = function_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, function_r, "api_yplot function set_name: object");
    struct yetty_api_yplot_function *fn = (struct yetty_api_yplot_function *)function_r.value;
    return store_field(fn->name, sizeof(fn->name), name, "api_yplot function: name too long");
}

YETTY_ANNOTATE("virtual@api_yplot:function:set_color")
YETTY_ANNOTATE("local@api_yplot:set_color")
static struct yetty_ycore_void_result function_set_color(struct yetty_yclass_object *obj,
                                                         const char *color)
{
    struct yetty_yclass_void_ptr_result function_r = function_from_obj(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, function_r, "api_yplot function set_color: object");
    struct yetty_api_yplot_function *fn = (struct yetty_api_yplot_function *)function_r.value;
    return store_field(fn->color, sizeof(fn->color), color, "api_yplot function: color too long");
}

/* A function has no owned heap (name/body/color are inline), so it needs no
 * class-specific destructor — the binding's generic destroy (yclass object
 * free) reclaims it. */

#include "yetty/gen/impl/api_yplot/plot.c"
