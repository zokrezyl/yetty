/* GENERATED — do not edit. */
#include <yetty/api/yview/view.h>

#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h> /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h> /* malloc/free for buffer marshalling */
#include <string.h> /* memcpy/strlen */

struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;
struct yetty_ycore_void_result yetty_yview_configure(struct yetty_yclass_object *obj, int fd,
                                                     uint32_t child_id, uint32_t kind,
                                                     uint32_t bg_color, float min_x, float min_y,
                                                     float max_x, float max_y);
struct yetty_ycore_void_result yetty_yview_set_content(
    struct yetty_yclass_object *obj, const struct yetty_ydraw_drawable_list *content);
struct yetty_ycore_void_result yetty_yview_set_text(struct yetty_yclass_object *obj,
                                                    const char *text, float font_size);
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

struct yetty_ycore_void_result yetty_yview_configure(struct yetty_yclass_object *obj, int fd,
                                                     uint32_t child_id, uint32_t kind,
                                                     uint32_t bg_color, float min_x, float min_y,
                                                     float max_x, float max_y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yview", (yetty_yclass_method_id_t)yetty_yview_configure);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_configure: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_configure: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yview_configure: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yview_configure: dispatch_lookup failed");
    return ((yetty_yview_configure_fn)dispatch_impl_r.value)(obj, fd, child_id, kind, bg_color,
                                                             min_x, min_y, max_x, max_y);
}

struct yetty_ycore_void_result yetty_yview_set_content(
    struct yetty_yclass_object *obj, const struct yetty_ydraw_drawable_list *content)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yview", (yetty_yclass_method_id_t)yetty_yview_set_content);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yview_set_content: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yview_set_content: dispatch_lookup failed");
    return ((yetty_yview_set_content_fn)dispatch_impl_r.value)(obj, content);
}

struct yetty_ycore_void_result yetty_yview_set_text(struct yetty_yclass_object *obj,
                                                    const char *text, float font_size)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yview", (yetty_yclass_method_id_t)yetty_yview_set_text);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_text: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_text: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yview_set_text: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yview_set_text: dispatch_lookup failed");
    return ((yetty_yview_set_text_fn)dispatch_impl_r.value)(obj, text, font_size);
}

struct yetty_ycore_void_result yetty_yview_set_plot(struct yetty_yclass_object *obj,
                                                    const char *expr, float x_min, float x_max,
                                                    float y_min, float y_max)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yview", (yetty_yclass_method_id_t)yetty_yview_set_plot);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_plot: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_plot: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yview_set_plot: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yview_set_plot: dispatch_lookup failed");
    return ((yetty_yview_set_plot_fn)dispatch_impl_r.value)(obj, expr, x_min, x_max, y_min, y_max);
}

struct yetty_ycore_void_result yetty_yview_set_content_size(struct yetty_yclass_object *obj,
                                                            float content_w, float content_h)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yview", (yetty_yclass_method_id_t)yetty_yview_set_content_size);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_yview_set_content_size: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_content_size: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yview_set_content_size: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yview_set_content_size: dispatch_lookup failed");
    return ((yetty_yview_set_content_size_fn)dispatch_impl_r.value)(obj, content_w, content_h);
}

struct yetty_ycore_void_result yetty_yview_scroll_to(struct yetty_yclass_object *obj,
                                                     float scroll_x, float scroll_y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yview", (yetty_yclass_method_id_t)yetty_yview_scroll_to);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_scroll_to: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_scroll_to: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yview_scroll_to: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yview_scroll_to: dispatch_lookup failed");
    return ((yetty_yview_scroll_to_fn)dispatch_impl_r.value)(obj, scroll_x, scroll_y);
}

struct yetty_ycore_void_result yetty_yview_scroll_by(struct yetty_yclass_object *obj, float delta_x,
                                                     float delta_y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yview", (yetty_yclass_method_id_t)yetty_yview_scroll_by);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_scroll_by: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_scroll_by: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yview_scroll_by: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yview_scroll_by: dispatch_lookup failed");
    return ((yetty_yview_scroll_by_fn)dispatch_impl_r.value)(obj, delta_x, delta_y);
}

struct yetty_ycore_void_result yetty_yview_set_rect(struct yetty_yclass_object *obj, float min_x,
                                                    float min_y, float max_x, float max_y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yview", (yetty_yclass_method_id_t)yetty_yview_set_rect);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_rect: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_set_rect: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yview_set_rect: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yview_set_rect: dispatch_lookup failed");
    return ((yetty_yview_set_rect_fn)dispatch_impl_r.value)(obj, min_x, min_y, max_x, max_y);
}

struct yetty_ycore_void_result yetty_yview_destroy(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_yview", (yetty_yclass_method_id_t)yetty_yview_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_yview_destroy: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yview_destroy: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_yview_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_yview_destroy: dispatch_lookup failed");
    return ((yetty_yview_destroy_fn)dispatch_impl_r.value)(obj);
}
