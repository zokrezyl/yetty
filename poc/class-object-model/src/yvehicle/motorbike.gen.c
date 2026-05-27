/* GENERATED — do not edit. */
#include "yvehicle/motorbike.h"
#include "yvehicle/vehicle.h"

const struct class *motorbike_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {
        .name = "motorbike",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct motorbike_data),
    };
    static const struct op ops[] = {
        {"vehicle_describe", (method_id_t)vehicle_describe, (impl_t)motorbike_describe},
    };
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         vehicle_class_get(), NULL, 0);
    return cls;
}
