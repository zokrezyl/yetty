/* GENERATED — do not edit. */
#include "yvehicle/electric.h"

const struct class *electric_mixin_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {
        .name = "yvehicle_electric",
        .type = CLASS_TYPE_MIXIN,
        .data_size = sizeof(struct electric_data),
    };
    static const struct op ops[] = {
        {"yvehicle_vehicle_brake", (method_id_t)vehicle_brake, (impl_t)electric_brake},
    };
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         NULL, NULL, 0);
    return cls;
}
