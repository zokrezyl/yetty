/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h>  /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h>  /* calloc/free for proxy + buffer marshalling */
#include <string.h>  /* memcpy/strcmp/strlen */

struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_ycore_void_result yetty_yview_configure(struct yetty_yclass_object * obj, int fd, uint32_t child_id, uint32_t kind, uint32_t bg_color, float min_x, float min_y, float max_x, float max_y);
struct yetty_ycore_void_result yetty_yview_set_content(struct yetty_yclass_object * obj, const struct yetty_ydraw_drawable_list * content);
struct yetty_ycore_void_result yetty_yview_set_text(struct yetty_yclass_object * obj, const char * text, float font_size);
struct yetty_ycore_void_result yetty_yview_set_plot(struct yetty_yclass_object * obj, const char * expr, float x_min, float x_max, float y_min, float y_max);
struct yetty_ycore_void_result yetty_yview_set_content_size(struct yetty_yclass_object * obj, float content_w, float content_h);
struct yetty_ycore_void_result yetty_yview_scroll_to(struct yetty_yclass_object * obj, float scroll_x, float scroll_y);
struct yetty_ycore_void_result yetty_yview_scroll_by(struct yetty_yclass_object * obj, float delta_x, float delta_y);
struct yetty_ycore_void_result yetty_yview_set_rect(struct yetty_yclass_object * obj, float min_x, float min_y, float max_x, float max_y);
struct yetty_ycore_void_result yetty_yview_destroy(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_yview_configure_fn)(struct yetty_yclass_object *, int, uint32_t, uint32_t, uint32_t, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_content_fn)(struct yetty_yclass_object *, const struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_void_result (*yetty_yview_set_text_fn)(struct yetty_yclass_object *, const char *, float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_plot_fn)(struct yetty_yclass_object *, const char *, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_content_size_fn)(struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_scroll_to_fn)(struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_scroll_by_fn)(struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_rect_fn)(struct yetty_yclass_object *, float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_destroy_fn)(struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_yview_configure_fn yetty_yview_view_yetty_yview_configure_check = view_configure;
YETTY_MAYBE_UNUSED
static yetty_yview_set_content_fn yetty_yview_view_yetty_yview_set_content_check = view_set_content;
YETTY_MAYBE_UNUSED
static yetty_yview_set_text_fn yetty_yview_view_yetty_yview_set_text_check = view_set_text;
YETTY_MAYBE_UNUSED
static yetty_yview_set_plot_fn yetty_yview_view_yetty_yview_set_plot_check = view_set_plot;
YETTY_MAYBE_UNUSED
static yetty_yview_set_content_size_fn yetty_yview_view_yetty_yview_set_content_size_check = view_set_content_size;
YETTY_MAYBE_UNUSED
static yetty_yview_scroll_to_fn yetty_yview_view_yetty_yview_scroll_to_check = view_scroll_to;
YETTY_MAYBE_UNUSED
static yetty_yview_scroll_by_fn yetty_yview_view_yetty_yview_scroll_by_check = view_scroll_by;
YETTY_MAYBE_UNUSED
static yetty_yview_set_rect_fn yetty_yview_view_yetty_yview_set_rect_check = view_set_rect;
YETTY_MAYBE_UNUSED
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

struct yetty_yclass_object_ptr_result yetty_yview_view_to(struct yetty_yview_view *data)
{
    if (!data)
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    struct yetty_yclass_ptr_result class_r = yetty_yview_view_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r, "yetty_yview_view_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r, "yetty_yview_view_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}


struct yetty_ycore_void_result yetty_yview_configure(struct yetty_yclass_object * obj, int fd, uint32_t child_id, uint32_t kind, uint32_t bg_color, float min_x, float min_y, float max_x, float max_y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yview", (yetty_yclass_method_id_t)yetty_yview_configure);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_configure: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yview_configure: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yview_configure: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yview_configure: dispatch_lookup failed");
    return ((yetty_yview_configure_fn)dispatch_impl_r.value)(obj, fd, child_id, kind, bg_color, min_x, min_y, max_x, max_y);
}

struct yetty_ycore_void_result yetty_yview_set_content(struct yetty_yclass_object * obj, const struct yetty_ydraw_drawable_list * content)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yview", (yetty_yclass_method_id_t)yetty_yview_set_content);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yview_set_content: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yview_set_content: dispatch_lookup failed");
    return ((yetty_yview_set_content_fn)dispatch_impl_r.value)(obj, content);
}

struct yetty_ycore_void_result yetty_yview_set_text(struct yetty_yclass_object * obj, const char * text, float font_size)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yview", (yetty_yclass_method_id_t)yetty_yview_set_text);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_text: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_text: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yview_set_text: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yview_set_text: dispatch_lookup failed");
    return ((yetty_yview_set_text_fn)dispatch_impl_r.value)(obj, text, font_size);
}

struct yetty_ycore_void_result yetty_yview_set_plot(struct yetty_yclass_object * obj, const char * expr, float x_min, float x_max, float y_min, float y_max)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yview", (yetty_yclass_method_id_t)yetty_yview_set_plot);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_plot: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_plot: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yview_set_plot: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yview_set_plot: dispatch_lookup failed");
    return ((yetty_yview_set_plot_fn)dispatch_impl_r.value)(obj, expr, x_min, x_max, y_min, y_max);
}

struct yetty_ycore_void_result yetty_yview_set_content_size(struct yetty_yclass_object * obj, float content_w, float content_h)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yview", (yetty_yclass_method_id_t)yetty_yview_set_content_size);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content_size: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content_size: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yview_set_content_size: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yview_set_content_size: dispatch_lookup failed");
    return ((yetty_yview_set_content_size_fn)dispatch_impl_r.value)(obj, content_w, content_h);
}

struct yetty_ycore_void_result yetty_yview_scroll_to(struct yetty_yclass_object * obj, float scroll_x, float scroll_y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yview", (yetty_yclass_method_id_t)yetty_yview_scroll_to);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_scroll_to: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yview_scroll_to: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yview_scroll_to: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yview_scroll_to: dispatch_lookup failed");
    return ((yetty_yview_scroll_to_fn)dispatch_impl_r.value)(obj, scroll_x, scroll_y);
}

struct yetty_ycore_void_result yetty_yview_scroll_by(struct yetty_yclass_object * obj, float delta_x, float delta_y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yview", (yetty_yclass_method_id_t)yetty_yview_scroll_by);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_scroll_by: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yview_scroll_by: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yview_scroll_by: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yview_scroll_by: dispatch_lookup failed");
    return ((yetty_yview_scroll_by_fn)dispatch_impl_r.value)(obj, delta_x, delta_y);
}

struct yetty_ycore_void_result yetty_yview_set_rect(struct yetty_yclass_object * obj, float min_x, float min_y, float max_x, float max_y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yview", (yetty_yclass_method_id_t)yetty_yview_set_rect);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_rect: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_rect: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yview_set_rect: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yview_set_rect: dispatch_lookup failed");
    return ((yetty_yview_set_rect_fn)dispatch_impl_r.value)(obj, min_x, min_y, max_x, max_y);
}

struct yetty_ycore_void_result yetty_yview_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_yview", (yetty_yclass_method_id_t)yetty_yview_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_yview_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_yview_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_yview_destroy: dispatch_lookup failed");
    return ((yetty_yview_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_yview_view_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_yview_view_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_yview_view");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_yview_view_class_get();
    if (YETTY_IS_ERR(class_accessor_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yview_view_create: class accessor failed", class_accessor_r);
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r =
            yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) return alloc_r;
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_yview_view");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(stderr,
                "yetty_yview_view_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_yview_view";
    struct yetty_ycore_size_result create_call_r = yetty_yclass_rpc_call(
        ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name, strlen(class_name), &handle,
        sizeof(handle));
    if (YETTY_IS_ERR(create_call_r))
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yview_view_create: CREATE call failed", create_call_r);
    if (create_call_r.value != sizeof(handle) || !handle)
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yview_view_create: CREATE returned no/invalid handle");

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy)
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yview_view_create: calloc(proxy) failed");
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}

