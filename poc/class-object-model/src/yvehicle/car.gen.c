/* GENERATED — do not edit. */
#include "yvehicle/car.h"
#include "yvehicle/vehicle.h"

__attribute__((unused))
static yvehicle_vehicle_describe_fn _yvehicle_car_yvehicle_vehicle_describe_check = car_describe;

const struct class *yvehicle_car_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {
        .name = "yvehicle_car",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct car_data),
    };
    static const struct op ops[] = {
        {"yvehicle", "vehicle_describe", (method_id_t)yvehicle_vehicle_describe, (impl_t)car_describe},
    };
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         yvehicle_vehicle_class_get(), NULL, 0);
    return cls;
}
