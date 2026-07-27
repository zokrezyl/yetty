/* GENERATED — do not edit. */
/* Public interface for regular class(es) `view` (module: yview).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YVIEW_VIEW_H
#define YETTY_YCLASSGEN_YVIEW_VIEW_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;

struct yetty_yclass_ptr_result yetty_yview_view_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yview_view;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YVIEW_VIEW_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YVIEW_VIEW_PTR_RESULT
struct yetty_yview_view_ptr_result {
    int ok;
    union {
        struct yetty_yview_view *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yview_view_ptr_result yetty_yview_view_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yview_view_to(struct yetty_yview_view *data);

/* configure: set the output fd, figure id/kind, opaque background, and rect.
 * Call once after create(), before set_content(). */
struct yetty_ycore_void_result yetty_yview_configure(struct yetty_yclass_object *obj, int fd,
                                                     uint32_t child_id, uint32_t kind,
                                                     uint32_t bg_color, float min_x, float min_y,
                                                     float max_x, float max_y);
struct yetty_ycore_void_result yetty_yview_set_content(
    struct yetty_yclass_object *obj, const struct yetty_ydraw_drawable_list *content);
/* set_text: render a UTF-8 string (newline-split into lines) as the view's
 * content using the server figure's default font. The convenience FFI callers
 * want when they have text but no drawable list (the pager / nvim case).
 * font_size <= 0 selects a default. */
struct yetty_ycore_void_result yetty_yview_set_text(struct yetty_yclass_object *obj,
                                                    const char *text, float font_size);
/* set_plot: render a yplot expression (yexpr-plot syntax — "sin(x)", or
 * "f=sin(x); g=cos(x)") as the view's content: one yplot figure filling the
 * rect (a plot doesn't scroll). x_max<=x_min or y_max<=y_min selects yplot's
 * default ranges. The ygrid figure renders the yplot composite prim via the
 * terminal's composite factory. */
struct yetty_ycore_void_result yetty_yview_set_plot(struct yetty_yclass_object *obj,
                                                    const char *expr, float x_min, float x_max,
                                                    float y_min, float y_max);
struct yetty_ycore_void_result yetty_yview_set_content_size(struct yetty_yclass_object *obj,
                                                            float content_w, float content_h);
struct yetty_ycore_void_result yetty_yview_scroll_to(struct yetty_yclass_object *obj,
                                                     float scroll_x, float scroll_y);
struct yetty_ycore_void_result yetty_yview_scroll_by(struct yetty_yclass_object *obj, float delta_x,
                                                     float delta_y);
struct yetty_ycore_void_result yetty_yview_set_rect(struct yetty_yclass_object *obj, float min_x,
                                                    float min_y, float max_x, float max_y);
/* destroy: clear the surface (DELETE_CHILD) and free the object. */
struct yetty_ycore_void_result yetty_yview_destroy(struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_yview_configure_fn)(struct yetty_yclass_object *,
                                                                   int, uint32_t, uint32_t,
                                                                   uint32_t, float, float, float,
                                                                   float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_content_fn)(
    struct yetty_yclass_object *, const struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_void_result (*yetty_yview_set_text_fn)(struct yetty_yclass_object *,
                                                                  const char *, float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_plot_fn)(struct yetty_yclass_object *,
                                                                  const char *, float, float, float,
                                                                  float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_content_size_fn)(
    struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_scroll_to_fn)(struct yetty_yclass_object *,
                                                                   float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_scroll_by_fn)(struct yetty_yclass_object *,
                                                                   float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_rect_fn)(struct yetty_yclass_object *,
                                                                  float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_destroy_fn)(struct yetty_yclass_object *);

struct yetty_yclass_object_ptr_result yetty_yview_view_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yview_register(void);

#ifdef __cplusplus
}
#endif

#endif
