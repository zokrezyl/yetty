/* GENERATED — do not edit. */
#include "yvehicle/vehicle.h"

const struct class *vehicle_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {
        .name = "vehicle",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct vehicle_data),
    };
    static const struct op ops[] = {
        {"vehicle_ctor", (method_id_t)vehicle_ctor, (impl_t)vehicle_default_ctor},
        {"vehicle_dtor", (method_id_t)vehicle_dtor, (impl_t)vehicle_default_dtor},
        {"vehicle_start", (method_id_t)vehicle_start, (impl_t)vehicle_default_start},
        {"vehicle_accelerate", (method_id_t)vehicle_accelerate, (impl_t)vehicle_default_accelerate},
        {"vehicle_brake", (method_id_t)vehicle_brake, (impl_t)vehicle_default_brake},
        {"vehicle_describe", (method_id_t)vehicle_describe, (impl_t)vehicle_default_describe},
    };
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         NULL, NULL, 0);
    return cls;
}
