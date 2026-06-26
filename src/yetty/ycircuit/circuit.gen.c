/* GENERATED — do not edit. */
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h> /* container_of, buffer */
#include <yetty/ytrace/ytrace.h>
#include <stdbool.h>
#include <stddef.h> /* NULL, size_t */
#include <stdint.h>
#include <stdio.h>  /* stderr */
#include <stdlib.h> /* calloc/free for proxy + buffer marshalling */
#include <string.h> /* memcpy/strcmp/strlen */

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;
struct yetty_ycore_void_result yetty_ycircuit_configure(struct yetty_yclass_object *obj,
                                                        float grid_px, uint32_t flags);
struct yetty_ycore_void_result yetty_ycircuit_parse(struct yetty_yclass_object *obj,
                                                    const char *input, size_t len);
struct yetty_ycore_void_result yetty_ycircuit_clear(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ycircuit_add_component(struct yetty_yclass_object *obj,
                                                           const char *kind, float x, float y,
                                                           int32_t rotation_deg, const char *name,
                                                           const char *value);
struct yetty_ycore_int_result yetty_ycircuit_add_ic(struct yetty_yclass_object *obj, float x,
                                                    float y, int32_t rotation_deg, const char *name,
                                                    const char *value, const char *pins_left,
                                                    const char *pins_right, const char *pins_top,
                                                    const char *pins_bottom);
struct yetty_ycore_int_result yetty_ycircuit_add_wire(struct yetty_yclass_object *obj, float x0,
                                                      float y0, float x1, float y1);
struct yetty_ycore_int_result yetty_ycircuit_add_junction(struct yetty_yclass_object *obj, float x,
                                                          float y);
struct yetty_ycore_int_result yetty_ycircuit_add_label(struct yetty_yclass_object *obj, float x,
                                                       float y, const char *text);
struct yetty_ydraw_drawable_list_result yetty_ycircuit_render(struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ycircuit_hit_test(struct yetty_yclass_object *obj, float x,
                                                      float y);
struct yetty_ycore_void_result yetty_ycircuit_set_highlight(struct yetty_yclass_object *obj,
                                                            int32_t element_id);
struct yetty_ycore_void_result yetty_ycircuit_destroy(struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_configure_fn)(struct yetty_yclass_object *,
                                                                      float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_parse_fn)(struct yetty_yclass_object *,
                                                                  const char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_clear_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_component_fn)(
    struct yetty_yclass_object *, const char *, float, float, int32_t, const char *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_ic_fn)(struct yetty_yclass_object *,
                                                                  float, float, int32_t,
                                                                  const char *, const char *,
                                                                  const char *, const char *,
                                                                  const char *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_wire_fn)(struct yetty_yclass_object *,
                                                                    float, float, float, float);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_junction_fn)(
    struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_label_fn)(struct yetty_yclass_object *,
                                                                     float, float, const char *);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ycircuit_render_fn)(
    struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_hit_test_fn)(struct yetty_yclass_object *,
                                                                    float, float);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_set_highlight_fn)(
    struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_destroy_fn)(struct yetty_yclass_object *);

YETTY_MAYBE_UNUSED
static yetty_ycircuit_configure_fn yetty_ycircuit_circuit_yetty_ycircuit_configure_check =
    circuit_configure;
YETTY_MAYBE_UNUSED
static yetty_ycircuit_parse_fn yetty_ycircuit_circuit_yetty_ycircuit_parse_check = circuit_parse;
YETTY_MAYBE_UNUSED
static yetty_ycircuit_clear_fn yetty_ycircuit_circuit_yetty_ycircuit_clear_check = circuit_clear;
YETTY_MAYBE_UNUSED
static yetty_ycircuit_add_component_fn yetty_ycircuit_circuit_yetty_ycircuit_add_component_check =
    circuit_add_component;
YETTY_MAYBE_UNUSED
static yetty_ycircuit_add_ic_fn yetty_ycircuit_circuit_yetty_ycircuit_add_ic_check = circuit_add_ic;
YETTY_MAYBE_UNUSED
static yetty_ycircuit_add_wire_fn yetty_ycircuit_circuit_yetty_ycircuit_add_wire_check =
    circuit_add_wire;
YETTY_MAYBE_UNUSED
static yetty_ycircuit_add_junction_fn yetty_ycircuit_circuit_yetty_ycircuit_add_junction_check =
    circuit_add_junction;
YETTY_MAYBE_UNUSED
static yetty_ycircuit_add_label_fn yetty_ycircuit_circuit_yetty_ycircuit_add_label_check =
    circuit_add_label;
YETTY_MAYBE_UNUSED
static yetty_ycircuit_render_fn yetty_ycircuit_circuit_yetty_ycircuit_render_check = circuit_render;
YETTY_MAYBE_UNUSED
static yetty_ycircuit_hit_test_fn yetty_ycircuit_circuit_yetty_ycircuit_hit_test_check =
    circuit_hit_test;
YETTY_MAYBE_UNUSED
static yetty_ycircuit_set_highlight_fn yetty_ycircuit_circuit_yetty_ycircuit_set_highlight_check =
    circuit_set_highlight;
YETTY_MAYBE_UNUSED
static yetty_ycircuit_destroy_fn yetty_ycircuit_circuit_yetty_ycircuit_destroy_check =
    circuit_obj_destroy;

struct yetty_yclass_ptr_result yetty_ycircuit_circuit_class_get(void)
{
    static const struct yetty_yclass *cls = NULL;
    if (cls) {
        return YETTY_OK(yetty_yclass_ptr, cls);
    }
    ydebug("registering class=yetty_ycircuit_circuit");

    static const struct yetty_yclass_descriptor desc = {
        .name = "yetty_ycircuit_circuit",
        .type = YETTY_YCLASS_TYPE_REGULAR,
        .data_size = sizeof(struct yetty_ycircuit_circuit),
        .data_align = _Alignof(struct yetty_ycircuit_circuit),
    };
    static const struct yetty_yclass_op ops[] = {
        {"yetty_ycircuit", "configure", (yetty_yclass_method_id_t)yetty_ycircuit_configure,
         (yetty_yclass_impl_t)circuit_configure},
        {"yetty_ycircuit", "parse", (yetty_yclass_method_id_t)yetty_ycircuit_parse,
         (yetty_yclass_impl_t)circuit_parse},
        {"yetty_ycircuit", "clear", (yetty_yclass_method_id_t)yetty_ycircuit_clear,
         (yetty_yclass_impl_t)circuit_clear},
        {"yetty_ycircuit", "add_component", (yetty_yclass_method_id_t)yetty_ycircuit_add_component,
         (yetty_yclass_impl_t)circuit_add_component},
        {"yetty_ycircuit", "add_ic", (yetty_yclass_method_id_t)yetty_ycircuit_add_ic,
         (yetty_yclass_impl_t)circuit_add_ic},
        {"yetty_ycircuit", "add_wire", (yetty_yclass_method_id_t)yetty_ycircuit_add_wire,
         (yetty_yclass_impl_t)circuit_add_wire},
        {"yetty_ycircuit", "add_junction", (yetty_yclass_method_id_t)yetty_ycircuit_add_junction,
         (yetty_yclass_impl_t)circuit_add_junction},
        {"yetty_ycircuit", "add_label", (yetty_yclass_method_id_t)yetty_ycircuit_add_label,
         (yetty_yclass_impl_t)circuit_add_label},
        {"yetty_ycircuit", "render", (yetty_yclass_method_id_t)yetty_ycircuit_render,
         (yetty_yclass_impl_t)circuit_render},
        {"yetty_ycircuit", "hit_test", (yetty_yclass_method_id_t)yetty_ycircuit_hit_test,
         (yetty_yclass_impl_t)circuit_hit_test},
        {"yetty_ycircuit", "set_highlight", (yetty_yclass_method_id_t)yetty_ycircuit_set_highlight,
         (yetty_yclass_impl_t)circuit_set_highlight},
        {"yetty_ycircuit", "destroy", (yetty_yclass_method_id_t)yetty_ycircuit_destroy,
         (yetty_yclass_impl_t)circuit_obj_destroy},
    };
    struct yetty_yclass_ptr_result register_class_r =
        yetty_yclass_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), NULL, NULL, 0);
    if (YETTY_IS_ERR(register_class_r)) {
        yerror("yetty_ycircuit_circuit_class_get: class_register failed: %s",
               register_class_r.error.msg);
        return YETTY_ERR(yetty_yclass_ptr,
                         "yetty_ycircuit_circuit_class_get: class_register failed",
                         register_class_r);
    }
    cls = register_class_r.value;
    return register_class_r;
}

struct yetty_ycircuit_circuit_ptr_result yetty_ycircuit_circuit_from(
    struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_r = yetty_ycircuit_circuit_class_get();
    if (YETTY_IS_ERR(class_r)) {
        return YETTY_ERR(yetty_ycircuit_circuit_ptr, "yetty_ycircuit_circuit_from: class accessor",
                         class_r);
    }
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, class_r.value);
    if (YETTY_IS_ERR(slice_r)) {
        return YETTY_ERR(yetty_ycircuit_circuit_ptr, "yetty_ycircuit_circuit_from: object_data",
                         slice_r);
    }
    return YETTY_OK(yetty_ycircuit_circuit_ptr, (struct yetty_ycircuit_circuit *)slice_r.value);
}

struct yetty_yclass_object_ptr_result yetty_ycircuit_circuit_to(struct yetty_ycircuit_circuit *data)
{
    if (!data) {
        return YETTY_OK(yetty_yclass_object_ptr, NULL);
    }
    struct yetty_yclass_ptr_result class_r = yetty_ycircuit_circuit_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_r,
                        "yetty_ycircuit_circuit_to: class accessor");
    struct yetty_ycore_size_result offset_r =
        yetty_yclass_object_data_offset(class_r.value, class_r.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, offset_r,
                        "yetty_ycircuit_circuit_to: data offset");
    return YETTY_OK(yetty_yclass_object_ptr,
                    (struct yetty_yclass_object *)((char *)data - offset_r.value));
}

struct yetty_ycore_void_result yetty_ycircuit_configure(struct yetty_yclass_object *obj,
                                                        float grid_px, uint32_t flags)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_configure);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_configure: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_configure: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ycircuit_configure: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ycircuit_configure: dispatch_lookup failed");
    return ((yetty_ycircuit_configure_fn)dispatch_impl_r.value)(obj, grid_px, flags);
}

struct yetty_ycore_void_result yetty_ycircuit_parse(struct yetty_yclass_object *obj,
                                                    const char *input, size_t len)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_parse);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_parse: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_parse: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ycircuit_parse: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ycircuit_parse: dispatch_lookup failed");
    return ((yetty_ycircuit_parse_fn)dispatch_impl_r.value)(obj, input, len);
}

struct yetty_ycore_void_result yetty_ycircuit_clear(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_clear);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_clear: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_clear: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ycircuit_clear: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ycircuit_clear: dispatch_lookup failed");
    return ((yetty_ycircuit_clear_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_int_result yetty_ycircuit_add_component(struct yetty_yclass_object *obj,
                                                           const char *kind, float x, float y,
                                                           int32_t rotation_deg, const char *name,
                                                           const char *value)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_add_component);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int,
                             "yetty_ycircuit_add_component: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_component: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                        "yetty_ycircuit_add_component: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                        "yetty_ycircuit_add_component: dispatch_lookup failed");
    return ((yetty_ycircuit_add_component_fn)dispatch_impl_r.value)(obj, kind, x, y, rotation_deg,
                                                                    name, value);
}

struct yetty_ycore_int_result yetty_ycircuit_add_ic(struct yetty_yclass_object *obj, float x,
                                                    float y, int32_t rotation_deg, const char *name,
                                                    const char *value, const char *pins_left,
                                                    const char *pins_right, const char *pins_top,
                                                    const char *pins_bottom)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_add_ic);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_ic: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_ic: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                        "yetty_ycircuit_add_ic: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                        "yetty_ycircuit_add_ic: dispatch_lookup failed");
    return ((yetty_ycircuit_add_ic_fn)dispatch_impl_r.value)(
        obj, x, y, rotation_deg, name, value, pins_left, pins_right, pins_top, pins_bottom);
}

struct yetty_ycore_int_result yetty_ycircuit_add_wire(struct yetty_yclass_object *obj, float x0,
                                                      float y0, float x1, float y1)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_add_wire);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_wire: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_wire: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                        "yetty_ycircuit_add_wire: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                        "yetty_ycircuit_add_wire: dispatch_lookup failed");
    return ((yetty_ycircuit_add_wire_fn)dispatch_impl_r.value)(obj, x0, y0, x1, y1);
}

struct yetty_ycore_int_result yetty_ycircuit_add_junction(struct yetty_yclass_object *obj, float x,
                                                          float y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_add_junction);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_junction: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_junction: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                        "yetty_ycircuit_add_junction: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                        "yetty_ycircuit_add_junction: dispatch_lookup failed");
    return ((yetty_ycircuit_add_junction_fn)dispatch_impl_r.value)(obj, x, y);
}

struct yetty_ycore_int_result yetty_ycircuit_add_label(struct yetty_yclass_object *obj, float x,
                                                       float y, const char *text)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_add_label);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_label: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_add_label: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                        "yetty_ycircuit_add_label: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                        "yetty_ycircuit_add_label: dispatch_lookup failed");
    return ((yetty_ycircuit_add_label_fn)dispatch_impl_r.value)(obj, x, y, text);
}

struct yetty_ydraw_drawable_list_result yetty_ycircuit_render(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_render);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ydraw_drawable_list,
                             "yetty_ycircuit_render: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ydraw_drawable_list, "yetty_ycircuit_render: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, object_class_r,
                        "yetty_ycircuit_render: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ydraw_drawable_list, dispatch_impl_r,
                        "yetty_ycircuit_render: dispatch_lookup failed");
    return ((yetty_ycircuit_render_fn)dispatch_impl_r.value)(obj);
}

struct yetty_ycore_int_result yetty_ycircuit_hit_test(struct yetty_yclass_object *obj, float x,
                                                      float y)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_hit_test);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_hit_test: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_int, "yetty_ycircuit_hit_test: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, object_class_r,
                        "yetty_ycircuit_hit_test: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, dispatch_impl_r,
                        "yetty_ycircuit_hit_test: dispatch_lookup failed");
    return ((yetty_ycircuit_hit_test_fn)dispatch_impl_r.value)(obj, x, y);
}

struct yetty_ycore_void_result yetty_ycircuit_set_highlight(struct yetty_yclass_object *obj,
                                                            int32_t element_id)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_set_highlight);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void,
                             "yetty_ycircuit_set_highlight: method_slot_get failed", method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_set_highlight: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ycircuit_set_highlight: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ycircuit_set_highlight: dispatch_lookup failed");
    return ((yetty_ycircuit_set_highlight_fn)dispatch_impl_r.value)(obj, element_id);
}

struct yetty_ycore_void_result yetty_ycircuit_destroy(struct yetty_yclass_object *obj)
{
    static yetty_yclass_method_slot method_slot = YETTY_YCLASS_METHOD_SLOT_UNDEFINED;
    if (method_slot == YETTY_YCLASS_METHOD_SLOT_UNDEFINED) {
        struct yetty_yclass_method_slot_result method_slot_r = yetty_yclass_method_slot_get(
            "yetty_ycircuit", (yetty_yclass_method_id_t)yetty_ycircuit_destroy);
        if (YETTY_IS_ERR(method_slot_r)) {
            return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_destroy: method_slot_get failed",
                             method_slot_r);
        }
        method_slot = method_slot_r.value;
    }

    if (!obj) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ycircuit_destroy: NULL object");
    }

    struct yetty_yclass_ptr_result object_class_r = yetty_yclass_object_class(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, object_class_r,
                        "yetty_ycircuit_destroy: object_class failed");
    struct yetty_yclass_impl_t_result dispatch_impl_r =
        yetty_yclass_dispatch_lookup(object_class_r.value, method_slot);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, dispatch_impl_r,
                        "yetty_ycircuit_destroy: dispatch_lookup failed");
    return ((yetty_ycircuit_destroy_fn)dispatch_impl_r.value)(obj);
}

struct yetty_yclass_object_ptr_result yetty_ycircuit_circuit_create(struct yetty_yclass_ctx *ctx);
struct yetty_yclass_object_ptr_result yetty_ycircuit_circuit_create(struct yetty_yclass_ctx *ctx)
{
    ydebug("class=yetty_ycircuit_circuit");
    /* Touch the local accessor first — registers the class's slots in
     * slot_table so subsequent name→local-slot lookups succeed.
     * Without this, translate_class on a fresh remote-only session
     * would have no local slots to map remote ids onto. */
    struct yetty_yclass_ptr_result class_accessor_r = yetty_ycircuit_circuit_class_get();
    if (YETTY_IS_ERR(class_accessor_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ycircuit_circuit_create: class accessor failed", class_accessor_r);
    }
    const struct yetty_yclass *klass = class_accessor_r.value;

    if (!ctx || !ctx->session) {
        struct yetty_yclass_object_ptr_result alloc_r = yetty_yclass_object_alloc(klass);
        if (YETTY_IS_ERR(alloc_r)) {
            return alloc_r;
        }
        return alloc_r;
    }

    /* Prefetch the class's local-id ↔ remote-id mapping. Not fatal
     * if it fails (the per-slot ensure_remote_id fallback can still
     * resolve ids on demand), but log so a malformed GET_CLASS
     * response isn't silently swallowed. */
    {
        struct yetty_ycore_void_result translate_class_r =
            yetty_yclass_rpc_session_translate_class(ctx->session, "yetty_ycircuit_circuit");
        if (YETTY_IS_ERR(translate_class_r)) {
            yetty_ycore_error_print(
                stderr,
                "yetty_ycircuit_circuit_create: translate_class (degraded — will lazy-resolve)",
                translate_class_r.error);
            yetty_ycore_error_destroy(translate_class_r.error);
        }
    }

    uint64_t handle = 0;
    const char *class_name = "yetty_ycircuit_circuit";
    struct yetty_ycore_size_result create_call_r =
        yetty_yclass_rpc_call(ctx->session, YETTY_YCLASS_RPC_OP_CREATE, 0, class_name,
                              strlen(class_name), &handle, sizeof(handle));
    if (YETTY_IS_ERR(create_call_r)) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ycircuit_circuit_create: CREATE call failed", create_call_r);
    }
    if (create_call_r.value != sizeof(handle) || !handle) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ycircuit_circuit_create: CREATE returned no/invalid handle");
    }

    /* Proxy: aligned (header + uint64_t) layout. Allocating raw bytes
     * and writing the handle past the header was misaligned on 32-bit
     * ABIs where sizeof(struct yetty_yclass_object) == 4. The proxy
     * struct in <yetty/yclass/class.h> uses natural alignment for both
     * fields. The class accessor is the same on both sides — proxies
     * never local-dispatch, so the class's data_size contract isn't
     * honoured for this allocation. */
    struct yetty_yclass_proxy *proxy = calloc(1, sizeof(*proxy));
    if (!proxy) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_ycircuit_circuit_create: calloc(proxy) failed");
    }
    proxy->header.klass = klass;
    /* Link the session onto the proxy so its methods marshal over it — they
     * read obj->session instead of taking a ctx argument. */
    proxy->header.session = ctx->session;
    proxy->handle = handle;
    return YETTY_OK(yetty_yclass_object_ptr, &proxy->header);
}
