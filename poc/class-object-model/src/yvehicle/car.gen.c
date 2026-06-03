/* GENERATED — do not edit. */
#include "yvehicle/car.h"
#include "yvehicle/vehicle.h"

__attribute__((unused))
static yvehicle_vehicle_describe_fn _yvehicle_car_yvehicle_vehicle_describe_check = car_describe;

struct class_ptr_result yvehicle_car_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YETTY_OK(class_ptr, cls);
    ydebug("registering class=yvehicle_car");

    static const struct class_descriptor desc = {
        .name = "yvehicle_car",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct car_data),
    };
    static const struct op ops[] = {
        {"yvehicle", "vehicle_describe", (method_id_t)yvehicle_vehicle_describe, (impl_t)car_describe},
    };
    struct class_ptr_result _parent_r = yvehicle_vehicle_class_get();
    if (YETTY_IS_ERR(_parent_r))
        return YETTY_ERR(class_ptr, "yvehicle_car_class_get: parent accessor failed", _parent_r);
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       _parent_r.value, NULL, 0);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(class_ptr, "yvehicle_car_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}

struct yvehicle_car_data_ptr_result yvehicle_car_data_get(struct object *obj)
{
    if (!obj) {
        return YETTY_ERR(yvehicle_car_data_ptr, "yvehicle_car_data_get: NULL object");
    }
    struct class_ptr_result class_result = yvehicle_car_class_get();
    YETTY_RETURN_IF_ERR(yvehicle_car_data_ptr, class_result, "yvehicle_car_data_get: class accessor failed");
    struct yetty_ycore_size_result offset_result =
        object_data_offset(object_class(obj), class_result.value);
    YETTY_RETURN_IF_ERR(yvehicle_car_data_ptr, offset_result, "yvehicle_car_data_get: object_data_offset failed");
    return YETTY_OK(yvehicle_car_data_ptr, (struct car_data *)((char *)obj + offset_result.value));
}

struct yetty_ycore_int_result yvehicle_car_doors_get(struct object *obj)
{
    struct yvehicle_car_data_ptr_result data = yvehicle_car_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data, "yvehicle_car_doors_get: data block");
    return YETTY_OK(yetty_ycore_int, data.value->doors);
}

struct yetty_ycore_void_result yvehicle_car_doors_set(struct object *obj, int value)
{
    struct yvehicle_car_data_ptr_result data = yvehicle_car_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "yvehicle_car_doors_set: data block");
    data.value->doors = value;
    return YETTY_OK_VOID();
}
