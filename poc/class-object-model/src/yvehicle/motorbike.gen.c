/* GENERATED — do not edit. */
#include "yvehicle/motorbike.h"
#include "yvehicle/vehicle.h"

const struct class *yvehicle_motorbike_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {
        .name = "yvehicle_motorbike",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct motorbike_data),
    };
    static const struct op ops[] = {
        {"yvehicle", "vehicle_describe", (method_id_t)yvehicle_vehicle_describe, (impl_t)motorbike_describe},
    };
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         yvehicle_vehicle_class_get(), NULL, 0);
    return cls;
}
