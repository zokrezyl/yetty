/* GENERATED — do not edit. */
#include "yvehicle/motorbike.h"
#include "yvehicle/vehicle.h"

__attribute__((unused))
static yvehicle_vehicle_describe_fn _yvehicle_motorbike_yvehicle_vehicle_describe_check = motorbike_describe;

struct class_ptr_result yvehicle_motorbike_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YETTY_OK(class_ptr, cls);
    ydebug("registering class=yvehicle_motorbike");

    static const struct class_descriptor desc = {
        .name = "yvehicle_motorbike",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct motorbike_data),
    };
    static const struct op ops[] = {
        {"yvehicle", "vehicle_describe", (method_id_t)yvehicle_vehicle_describe, (impl_t)motorbike_describe},
    };
    struct class_ptr_result _parent_r = yvehicle_vehicle_class_get();
    if (YETTY_IS_ERR(_parent_r))
        return YETTY_ERR(class_ptr, "yvehicle_motorbike_class_get: parent accessor failed", _parent_r);
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       _parent_r.value, NULL, 0);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(class_ptr, "yvehicle_motorbike_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}

struct motorbike_data *yvehicle_motorbike_data(struct object *obj)
{
    if (!obj) {
        ydebug("yvehicle_motorbike_data: NULL object");
        return NULL;
    }
    struct class_ptr_result class_result = yvehicle_motorbike_class_get();
    if (YETTY_IS_ERR(class_result)) {
        yetty_ycore_error_print(stderr, "yvehicle_motorbike_data", class_result.error);
        yetty_ycore_error_destroy(class_result.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_result =
        object_data_offset(object_class(obj), class_result.value);
    if (YETTY_IS_ERR(offset_result)) {
        yetty_ycore_error_print(stderr, "yvehicle_motorbike_data", offset_result.error);
        yetty_ycore_error_destroy(offset_result.error);
        return NULL;
    }
    return (struct motorbike_data *)((char *)obj + offset_result.value);
}

int yvehicle_motorbike_has_sidecar_get(struct object *obj)
{
    struct motorbike_data *data = yvehicle_motorbike_data(obj);
    if (!data) {
        ydebug("yvehicle_motorbike_has_sidecar_get: no data block for obj=%p", (void *)obj);
        int fallback = {0};
        return fallback;
    }
    return data->has_sidecar;
}

void yvehicle_motorbike_has_sidecar_set(struct object *obj, int value)
{
    struct motorbike_data *data = yvehicle_motorbike_data(obj);
    if (!data) {
        ydebug("yvehicle_motorbike_has_sidecar_set: no data block for obj=%p", (void *)obj);
        return;
    }
    data->has_sidecar = value;
}
