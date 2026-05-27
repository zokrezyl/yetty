/* GENERATED — do not edit. */
#include "ytuning/tuned_sportscar.h"
#include "yvehicle/methods.gen.h"
#include "yvehicle/sportscar.h"

const struct class *ytuning_tuned_sportscar_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {
        .name = "ytuning_tuned_sportscar",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct tuned_sportscar_data),
    };
    static const struct op ops[] = {
        {"yvehicle", "vehicle_describe", (method_id_t)yvehicle_vehicle_describe, (impl_t)tuned_sportscar_describe},
    };
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         yvehicle_sportscar_class_get(), NULL, 0);
    return cls;
}
