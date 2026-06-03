/* GENERATED — do not edit. */
#include "yvehicle/vehicle.h"

__attribute__((unused))
static yvehicle_vehicle_ctor_fn _yvehicle_vehicle_yvehicle_vehicle_ctor_check = vehicle_default_ctor;
__attribute__((unused))
static yvehicle_vehicle_dtor_fn _yvehicle_vehicle_yvehicle_vehicle_dtor_check = vehicle_default_dtor;
__attribute__((unused))
static yvehicle_vehicle_start_fn _yvehicle_vehicle_yvehicle_vehicle_start_check = vehicle_default_start;
__attribute__((unused))
static yvehicle_vehicle_accelerate_fn _yvehicle_vehicle_yvehicle_vehicle_accelerate_check = vehicle_default_accelerate;
__attribute__((unused))
static yvehicle_vehicle_brake_fn _yvehicle_vehicle_yvehicle_vehicle_brake_check = vehicle_default_brake;
__attribute__((unused))
static yvehicle_vehicle_describe_fn _yvehicle_vehicle_yvehicle_vehicle_describe_check = vehicle_default_describe;

struct class_ptr_result yvehicle_vehicle_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YETTY_OK(class_ptr, cls);
    ydebug("registering class=yvehicle_vehicle");

    static const struct class_descriptor desc = {
        .name = "yvehicle_vehicle",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct vehicle_data),
    };
    static const struct op ops[] = {
        {"yvehicle", "vehicle_ctor", (method_id_t)yvehicle_vehicle_ctor, (impl_t)vehicle_default_ctor},
        {"yvehicle", "vehicle_dtor", (method_id_t)yvehicle_vehicle_dtor, (impl_t)vehicle_default_dtor},
        {"yvehicle", "vehicle_start", (method_id_t)yvehicle_vehicle_start, (impl_t)vehicle_default_start},
        {"yvehicle", "vehicle_accelerate", (method_id_t)yvehicle_vehicle_accelerate, (impl_t)vehicle_default_accelerate},
        {"yvehicle", "vehicle_brake", (method_id_t)yvehicle_vehicle_brake, (impl_t)vehicle_default_brake},
        {"yvehicle", "vehicle_describe", (method_id_t)yvehicle_vehicle_describe, (impl_t)vehicle_default_describe},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(class_ptr, "yvehicle_vehicle_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}

struct vehicle_data *yvehicle_vehicle_data(struct object *obj)
{
    if (!obj) {
        ydebug("yvehicle_vehicle_data: NULL object");
        return NULL;
    }
    struct class_ptr_result class_result = yvehicle_vehicle_class_get();
    if (YETTY_IS_ERR(class_result)) {
        yetty_ycore_error_print(stderr, "yvehicle_vehicle_data", class_result.error);
        yetty_ycore_error_destroy(class_result.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_result =
        object_data_offset(object_class(obj), class_result.value);
    if (YETTY_IS_ERR(offset_result)) {
        yetty_ycore_error_print(stderr, "yvehicle_vehicle_data", offset_result.error);
        yetty_ycore_error_destroy(offset_result.error);
        return NULL;
    }
    return (struct vehicle_data *)((char *)obj + offset_result.value);
}

int yvehicle_vehicle_mileage_get(struct object *obj)
{
    struct vehicle_data *data = yvehicle_vehicle_data(obj);
    if (!data) {
        ydebug("yvehicle_vehicle_mileage_get: no data block for obj=%p", (void *)obj);
        int fallback = {0};
        return fallback;
    }
    return data->mileage;
}

void yvehicle_vehicle_mileage_set(struct object *obj, int value)
{
    struct vehicle_data *data = yvehicle_vehicle_data(obj);
    if (!data) {
        ydebug("yvehicle_vehicle_mileage_set: no data block for obj=%p", (void *)obj);
        return;
    }
    data->mileage = value;
}

int yvehicle_vehicle_speed_get(struct object *obj)
{
    struct vehicle_data *data = yvehicle_vehicle_data(obj);
    if (!data) {
        ydebug("yvehicle_vehicle_speed_get: no data block for obj=%p", (void *)obj);
        int fallback = {0};
        return fallback;
    }
    return data->speed;
}

void yvehicle_vehicle_speed_set(struct object *obj, int value)
{
    struct vehicle_data *data = yvehicle_vehicle_data(obj);
    if (!data) {
        ydebug("yvehicle_vehicle_speed_set: no data block for obj=%p", (void *)obj);
        return;
    }
    data->speed = value;
}
