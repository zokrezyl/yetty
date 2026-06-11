/* GENERATED — do not edit. */
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>

struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_ycore_void_result yetty_yview_configure(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, int fd, uint32_t child_id, uint32_t kind, uint32_t bg_color, float min_x, float min_y, float max_x, float max_y);
struct yetty_ycore_void_result yetty_yview_set_content(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, const struct yetty_ydraw_drawable_list * content);
struct yetty_ycore_void_result yetty_yview_set_text(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, const char * text, float font_size);
struct yetty_ycore_void_result yetty_yview_set_plot(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, const char * expr, float x_min, float x_max, float y_min, float y_max);
struct yetty_ycore_void_result yetty_yview_set_content_size(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, float content_w, float content_h);
struct yetty_ycore_void_result yetty_yview_scroll_to(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, float scroll_x, float scroll_y);
struct yetty_ycore_void_result yetty_yview_scroll_by(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, float delta_x, float delta_y);
struct yetty_ycore_void_result yetty_yview_set_rect(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, float min_x, float min_y, float max_x, float max_y);
struct yetty_ycore_void_result yetty_yview_destroy(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_yview_configure_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, int, uint32_t, uint32_t, uint32_t, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_content_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, const struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_void_result (*yetty_yview_set_text_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, const char *, float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_plot_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, const char *, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_content_size_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_scroll_to_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_scroll_by_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_rect_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_destroy_fn)(struct yetty_yclass_ctx *, struct yetty_yclass_object *);

[[maybe_unused]]
static yetty_yview_configure_fn yetty_yview_view_yetty_yview_configure_check = view_configure;
[[maybe_unused]]
static yetty_yview_set_content_fn yetty_yview_view_yetty_yview_set_content_check = view_set_content;
[[maybe_unused]]
static yetty_yview_set_text_fn yetty_yview_view_yetty_yview_set_text_check = view_set_text;
[[maybe_unused]]
static yetty_yview_set_plot_fn yetty_yview_view_yetty_yview_set_plot_check = view_set_plot;
[[maybe_unused]]
static yetty_yview_set_content_size_fn yetty_yview_view_yetty_yview_set_content_size_check = view_set_content_size;
[[maybe_unused]]
static yetty_yview_scroll_to_fn yetty_yview_view_yetty_yview_scroll_to_check = view_scroll_to;
[[maybe_unused]]
static yetty_yview_scroll_by_fn yetty_yview_view_yetty_yview_scroll_by_check = view_scroll_by;
[[maybe_unused]]
static yetty_yview_set_rect_fn yetty_yview_view_yetty_yview_set_rect_check = view_set_rect;
[[maybe_unused]]
static yetty_yview_destroy_fn yetty_yview_view_yetty_yview_destroy_check = view_destroy;

struct yetty_yclass_ptr_result yetty_yview_view_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) return YETTY_OK(yetty_yclass_ptr, cls);
    ydebug("registering class=yetty_yview_view");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_yview_view",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_yview_view),
        .data_align = _Alignof(struct yetty_yview_view),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_yview", "configure", (yetty_yclass_method_id_t)yetty_yview_configure, (yetty_yclass_impl_t)view_configure},
        {"yetty_yview", "set_content", (yetty_yclass_method_id_t)yetty_yview_set_content, (yetty_yclass_impl_t)view_set_content},
        {"yetty_yview", "set_text", (yetty_yclass_method_id_t)yetty_yview_set_text, (yetty_yclass_impl_t)view_set_text},
        {"yetty_yview", "set_plot", (yetty_yclass_method_id_t)yetty_yview_set_plot, (yetty_yclass_impl_t)view_set_plot},
        {"yetty_yview", "set_content_size", (yetty_yclass_method_id_t)yetty_yview_set_content_size, (yetty_yclass_impl_t)view_set_content_size},
        {"yetty_yview", "scroll_to", (yetty_yclass_method_id_t)yetty_yview_scroll_to, (yetty_yclass_impl_t)view_scroll_to},
        {"yetty_yview", "scroll_by", (yetty_yclass_method_id_t)yetty_yview_scroll_by, (yetty_yclass_impl_t)view_scroll_by},
        {"yetty_yview", "set_rect", (yetty_yclass_method_id_t)yetty_yview_set_rect, (yetty_yclass_impl_t)view_set_rect},
        {"yetty_yview", "destroy", (yetty_yclass_method_id_t)yetty_yview_destroy, (yetty_yclass_impl_t)view_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                              NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_yview_view_class_get: class_register failed: %s", register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr, "yetty_yview_view_class_get: class_register failed", register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_yview_view_ptr_result yetty_yview_view_from(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_yview_view_class_get();
    if (YETTY_IS_ERR(class_r))
        return YETTY_ERR(yetty_yview_view_ptr, "yetty_yview_view_from: class accessor", class_r);
    struct yetty_yclass_void_ptr_result slice_r =
        yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yview_view_ptr, "yetty_yview_view_from: object_data", slice_r);
    return YETTY_OK(yetty_yview_view_ptr, (struct yetty_yview_view *)slice_r.value);
}

struct yetty_yclass_object *yetty_yview_view_to(struct yetty_yview_view *data)
{
    if (!data)
        return NULL;
    struct yetty_yclass_ptr_result class_r = yetty_yview_view_class_get();
    if (YETTY_IS_ERR(class_r)) {
        yetty_ycore_error_destroy(class_r.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    if (YETTY_IS_ERR(offset_r)) {
        yetty_ycore_error_destroy(offset_r.error);
        return NULL;
    }
    return (struct yetty_yclass_object *)((char *)data - offset_r.value);
}
