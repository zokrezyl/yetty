/* GENERATED — do not edit. */
#include "yvehicle/car.h"
#include "yvehicle/electric.h"
#include "yvehicle/sportscar.h"

const struct class *yvehicle_sportscar_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {
        .name = "yvehicle_sportscar",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct sportscar_data),
    };
    static const struct op ops[] = {
        {"yvehicle", "vehicle_describe", (method_id_t)yvehicle_vehicle_describe, (impl_t)sportscar_describe},
        {"yvehicle", "vehicle_accelerate", (method_id_t)yvehicle_vehicle_accelerate, (impl_t)sportscar_accelerate},
    };
    const struct class *mixins[] = { yvehicle_electric_mixin_get() };
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         yvehicle_car_class_get(), mixins, 1);
    return cls;
}
