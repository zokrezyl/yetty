/* GENERATED — do not edit. */
#include <yetty/api/ycircuit/circuit.h>

#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>  /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h>  /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h>  /* malloc/free for buffer marshalling */
#include <string.h>  /* memcpy/strlen */

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;
struct yetty_ycore_void_result yetty_ycircuit_configure(struct yetty_yclass_object * obj, float grid_px, uint32_t flags);
struct yetty_ycore_void_result yetty_ycircuit_parse(struct yetty_yclass_object * obj, const char * input, size_t len);
struct yetty_ycore_void_result yetty_ycircuit_clear(struct yetty_yclass_object * obj);
struct yetty_ycore_int_result yetty_ycircuit_add_component(struct yetty_yclass_object * obj, const char * kind, float x, float y, int32_t rotation_deg, const char * name, const char * value);
struct yetty_ycore_int_result yetty_ycircuit_add_ic(struct yetty_yclass_object * obj, float x, float y, int32_t rotation_deg, const char * name, const char * value, const char * pins_left, const char * pins_right, const char * pins_top, const char * pins_bottom);
struct yetty_ycore_int_result yetty_ycircuit_add_wire(struct yetty_yclass_object * obj, float x0, float y0, float x1, float y1);
struct yetty_ycore_int_result yetty_ycircuit_add_junction(struct yetty_yclass_object * obj, float x, float y);
struct yetty_ycore_int_result yetty_ycircuit_add_label(struct yetty_yclass_object * obj, float x, float y, const char * text);
struct yetty_ydraw_drawable_list_result yetty_ycircuit_render(struct yetty_yclass_object * obj);
struct yetty_ycore_int_result yetty_ycircuit_hit_test(struct yetty_yclass_object * obj, float x, float y);
struct yetty_ycore_void_result yetty_ycircuit_set_highlight(struct yetty_yclass_object * obj, int32_t element_id);
struct yetty_ycore_void_result yetty_ycircuit_destroy(struct yetty_yclass_object * obj);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_configure_fn)(struct yetty_yclass_object *, float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_parse_fn)(struct yetty_yclass_object *, const char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_clear_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_component_fn)(struct yetty_yclass_object *, const char *, float, float, int32_t, const char *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_ic_fn)(struct yetty_yclass_object *, float, float, int32_t, const char *, const char *, const char *, const char *, const char *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_wire_fn)(struct yetty_yclass_object *, float, float, float, float);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_junction_fn)(struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_label_fn)(struct yetty_yclass_object *, float, float, const char *);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ycircuit_render_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_hit_test_fn)(struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_set_highlight_fn)(struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_destroy_fn)(struct yetty_yclass_object *);

struct yetty_ycore_void_result yetty_ycircuit_configure(struct yetty_yclass_object * obj, float grid_px, uint32_t flags)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_configure);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_configure: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_configure: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ycircuit_configure: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ycircuit_configure: dispatch_lookup failed");
    return ((yetty_ycircuit_configure_fn)dispatch_impl_r.value)(obj, grid_px, flags);
}

struct yetty_ycore_void_result yetty_ycircuit_parse(struct yetty_yclass_object * obj, const char * input, size_t len)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_parse);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_parse: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_parse: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ycircuit_parse: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ycircuit_parse: dispatch_lookup failed");
    return ((yetty_ycircuit_parse_fn)dispatch_impl_r.value)(obj, input, len);
}

struct yetty_ycore_void_result yetty_ycircuit_clear(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_clear);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_clear: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_clear: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ycircuit_clear: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ycircuit_clear: dispatch_lookup failed");
    return ((yetty_ycircuit_clear_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_int_result yetty_ycircuit_add_component(struct yetty_yclass_object * obj, const char * kind, float x, float y, int32_t rotation_deg, const char * name, const char * value)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_add_component);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_component: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_component: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ycircuit_add_component: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ycircuit_add_component: dispatch_lookup failed");
    return ((yetty_ycircuit_add_component_fn)dispatch_impl_r.value)(obj, kind, x, y, rotation_deg, name, value);
}

struct yetty_ycore_int_result yetty_ycircuit_add_ic(struct yetty_yclass_object * obj, float x, float y, int32_t rotation_deg, const char * name, const char * value, const char * pins_left, const char * pins_right, const char * pins_top, const char * pins_bottom)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_add_ic);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_ic: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_ic: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ycircuit_add_ic: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ycircuit_add_ic: dispatch_lookup failed");
    return ((yetty_ycircuit_add_ic_fn)dispatch_impl_r.value)(obj, x, y, rotation_deg, name, value, pins_left, pins_right, pins_top, pins_bottom);
}

struct yetty_ycore_int_result yetty_ycircuit_add_wire(struct yetty_yclass_object * obj, float x0, float y0, float x1, float y1)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_add_wire);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_wire: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_wire: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ycircuit_add_wire: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ycircuit_add_wire: dispatch_lookup failed");
    return ((yetty_ycircuit_add_wire_fn)dispatch_impl_r.value)(obj, x0, y0, x1, y1);
}

struct yetty_ycore_int_result yetty_ycircuit_add_junction(struct yetty_yclass_object * obj, float x, float y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_add_junction);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_junction: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_junction: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ycircuit_add_junction: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ycircuit_add_junction: dispatch_lookup failed");
    return ((yetty_ycircuit_add_junction_fn)dispatch_impl_r.value)(obj, x, y);
}

struct yetty_ycore_int_result yetty_ycircuit_add_label(struct yetty_yclass_object * obj, float x, float y, const char * text)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_add_label);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_label: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_label: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ycircuit_add_label: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ycircuit_add_label: dispatch_lookup failed");
    return ((yetty_ycircuit_add_label_fn)dispatch_impl_r.value)(obj, x, y, text);
}

struct yetty_ydraw_drawable_list_result yetty_ycircuit_render(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_render);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ydraw_drawable_list, "yetty_ycircuit_render: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ydraw_drawable_list, "yetty_ycircuit_render: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, object_class_r, "yetty_ycircuit_render: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, dispatch_impl_r, "yetty_ycircuit_render: dispatch_lookup failed");
    return ((yetty_ycircuit_render_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_int_result yetty_ycircuit_hit_test(struct yetty_yclass_object * obj, float x, float y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_hit_test);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_hit_test: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_hit_test: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r, "yetty_ycircuit_hit_test: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r, "yetty_ycircuit_hit_test: dispatch_lookup failed");
    return ((yetty_ycircuit_hit_test_fn)dispatch_impl_r.value)(obj, x, y);
}

struct yetty_ycore_void_result yetty_ycircuit_set_highlight(struct yetty_yclass_object * obj, int32_t element_id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_set_highlight);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_set_highlight: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_set_highlight: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ycircuit_set_highlight: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ycircuit_set_highlight: dispatch_lookup failed");
    return ((yetty_ycircuit_set_highlight_fn)dispatch_impl_r.value)(obj, element_id);
}

struct yetty_ycore_void_result yetty_ycircuit_destroy(struct yetty_yclass_object * obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r =
            yetty_yclass_method_slot_get("yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_destroy);
        if (YETTY_IS_ERR(method_slot_r))
            return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_destroy: method_slot_get failed", method_slot_r);
        method_slot = method_slot_r.value;
    }

    if (!obj) return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_destroy: NULL object");

    struct yetty_yclass_ptr_result object_class_r =
        yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r, "yetty_ycircuit_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r, "yetty_ycircuit_destroy: dispatch_lookup failed");
    return ((yetty_ycircuit_destroy_fn)dispatch_impl_r.value)(obj);
}

