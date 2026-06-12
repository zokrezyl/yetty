/* GENERATED — do not edit. */
#include <yetty/ycore/result.h>
#include <yetty/ytrace/ytrace.h>
#include <stddef.h> /* NULL, size_t */

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;
struct yetty_ycore_void_result yetty_ycircuit_configure(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *obj,
                                                        float grid_px, uint32_t flags);
struct yetty_ycore_void_result yetty_ycircuit_parse(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj,
                                                    const char *input, size_t len);
struct yetty_ycore_void_result yetty_ycircuit_clear(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ycircuit_add_component(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj,
                                                           const char *kind, float x, float y,
                                                           int32_t rotation_deg, const char *name,
                                                           const char *value);
struct yetty_ycore_int_result yetty_ycircuit_add_ic(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj, float x,
                                                    float y, int32_t rotation_deg, const char *name,
                                                    const char *value, const char *pins_left,
                                                    const char *pins_right, const char *pins_top,
                                                    const char *pins_bottom);
struct yetty_ycore_int_result yetty_ycircuit_add_wire(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj, float x0,
                                                      float y0, float x1, float y1);
struct yetty_ycore_int_result yetty_ycircuit_add_junction(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj, float x,
                                                          float y);
struct yetty_ycore_int_result yetty_ycircuit_add_label(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj, float x,
                                                       float y, const char *text);
struct yetty_ydraw_drawable_list_result yetty_ycircuit_render(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ycircuit_hit_test(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj, float x,
                                                      float y);
struct yetty_ycore_void_result yetty_ycircuit_set_highlight(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj,
                                                            int32_t element_id);
struct yetty_ycore_void_result yetty_ycircuit_destroy(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_configure_fn)(struct yetty_yclass_ctx *,
                                                                      struct yetty_yclass_object *,
                                                                      float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_parse_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *,
                                                                  const char *, size_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_clear_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_component_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, const char *, float, float, int32_t,
    const char *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_ic_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float, int32_t, const char *,
    const char *, const char *, const char *, const char *, const char *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_wire_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    float, float, float, float);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_junction_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_add_label_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *,
                                                                     float, float, const char *);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ycircuit_render_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ycircuit_hit_test_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    float, float);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_set_highlight_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_ycircuit_destroy_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *);

[[maybe_unused]]
static yetty_ycircuit_configure_fn yetty_ycircuit_circuit_yetty_ycircuit_configure_check =
    circuit_configure;
[[maybe_unused]]
static yetty_ycircuit_parse_fn yetty_ycircuit_circuit_yetty_ycircuit_parse_check = circuit_parse;
[[maybe_unused]]
static yetty_ycircuit_clear_fn yetty_ycircuit_circuit_yetty_ycircuit_clear_check = circuit_clear;
[[maybe_unused]]
static yetty_ycircuit_add_component_fn yetty_ycircuit_circuit_yetty_ycircuit_add_component_check =
    circuit_add_component;
[[maybe_unused]]
static yetty_ycircuit_add_ic_fn yetty_ycircuit_circuit_yetty_ycircuit_add_ic_check = circuit_add_ic;
[[maybe_unused]]
static yetty_ycircuit_add_wire_fn yetty_ycircuit_circuit_yetty_ycircuit_add_wire_check =
    circuit_add_wire;
[[maybe_unused]]
static yetty_ycircuit_add_junction_fn yetty_ycircuit_circuit_yetty_ycircuit_add_junction_check =
    circuit_add_junction;
[[maybe_unused]]
static yetty_ycircuit_add_label_fn yetty_ycircuit_circuit_yetty_ycircuit_add_label_check =
    circuit_add_label;
[[maybe_unused]]
static yetty_ycircuit_render_fn yetty_ycircuit_circuit_yetty_ycircuit_render_check = circuit_render;
[[maybe_unused]]
static yetty_ycircuit_hit_test_fn yetty_ycircuit_circuit_yetty_ycircuit_hit_test_check =
    circuit_hit_test;
[[maybe_unused]]
static yetty_ycircuit_set_highlight_fn yetty_ycircuit_circuit_yetty_ycircuit_set_highlight_check =
    circuit_set_highlight;
[[maybe_unused]]
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

struct yetty_yclass_object *yetty_ycircuit_circuit_to(struct yetty_ycircuit_circuit *data)
{
    if (!data) {
        return NULL;
    }
    struct yetty_yclass_ptr_result class_r = yetty_ycircuit_circuit_class_get();
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
