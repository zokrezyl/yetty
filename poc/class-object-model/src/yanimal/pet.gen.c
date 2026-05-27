/* GENERATED — do not edit. */
#include "yanimal/pet.h"

const struct class *pet_mixin_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {
        .name = "yanimal_pet",
        .type = CLASS_TYPE_MIXIN,
        .data_size = sizeof(struct pet_data),
    };
    static const struct op ops[] = {
        {"yanimal_animal_eat", (method_id_t)animal_eat, (impl_t)pet_eat},
    };
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         NULL, NULL, 0);
    return cls;
}
