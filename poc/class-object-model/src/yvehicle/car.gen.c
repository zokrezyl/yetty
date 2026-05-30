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
