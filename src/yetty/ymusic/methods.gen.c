/* GENERATED — do not edit. */
#include "yetty/ymusic/methods.gen.h"
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of */
#include <yetty/ytrace/ytrace.h>
#include <stdint.h>
#include <stdlib.h>  /* malloc/free for buffer-arg marshalling */
#include <string.h>

struct yetty_ycore_void_result yetty_ymusic_configure(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, float width, float staff_space, uint32_t flags)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymusic", (yetty_yclass_method_id_t)yetty_ymusic_configure);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ymusic_configure: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ymusic_configure: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ymusic_configure: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ymusic_configure: dispatch_lookup failed");
    return ((yetty_ymusic_configure_fn)dispatch_impl_r.value)(ctx, obj, width, staff_space, flags);
}

struct yetty_ycore_void_result yetty_ymusic_set_font_path(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, const char * path)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymusic", (yetty_yclass_method_id_t)yetty_ymusic_set_font_path);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ymusic_set_font_path: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ymusic_set_font_path: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ymusic_set_font_path: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ymusic_set_font_path: dispatch_lookup failed");
    return ((yetty_ymusic_set_font_path_fn)dispatch_impl_r.value)(ctx, obj, path);
}

struct yetty_ycore_void_result yetty_ymusic_parse(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, const char * input, size_t len)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymusic", (yetty_yclass_method_id_t)yetty_ymusic_parse);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ymusic_parse: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ymusic_parse: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ymusic_parse: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ymusic_parse: dispatch_lookup failed");
    return ((yetty_ymusic_parse_fn)dispatch_impl_r.value)(ctx, obj, input, len);
}

struct yetty_ydraw_drawable_list_result yetty_ymusic_render(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymusic", (yetty_yclass_method_id_t)yetty_ymusic_render);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ydraw_drawable_list, "yetty_ymusic_render: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ydraw_drawable_list, "yetty_ymusic_render: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, object_class_r, "yetty_ymusic_render: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, dispatch_impl_r, "yetty_ymusic_render: dispatch_lookup failed");
    return ((yetty_ymusic_render_fn)dispatch_impl_r.value)(ctx, obj);
}

struct yetty_ycore_int_result yetty_ymusic_hit_test(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, float x, float y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymusic", (yetty_yclass_method_id_t)yetty_ymusic_hit_test);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ymusic_hit_test: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ymusic_hit_test: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ymusic_hit_test: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ymusic_hit_test: dispatch_lookup failed");
    return ((yetty_ymusic_hit_test_fn)dispatch_impl_r.value)(ctx, obj, x, y);
}

struct yetty_ycore_void_result yetty_ymusic_set_highlight(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj, int32_t element_id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymusic", (yetty_yclass_method_id_t)yetty_ymusic_set_highlight);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ymusic_set_highlight: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ymusic_set_highlight: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ymusic_set_highlight: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ymusic_set_highlight: dispatch_lookup failed");
    return ((yetty_ymusic_set_highlight_fn)dispatch_impl_r.value)(ctx, obj, element_id);
}

struct yetty_ycore_void_result yetty_ymusic_destroy(struct yetty_yclass_ctx * ctx, struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ymusic", (yetty_yclass_method_id_t)yetty_ymusic_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ymusic_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ymusic_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ymusic_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ymusic_destroy: dispatch_lookup failed");
    return ((yetty_ymusic_destroy_fn)dispatch_impl_r.value)(ctx, obj);
}

