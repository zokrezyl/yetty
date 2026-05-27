/* GENERATED — do not edit. */
#include "yanimal/animal.h"

const struct class *animal_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {
        .name = "yanimal_animal",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct animal_data),
    };
    static const struct op ops[] = {
        {"yanimal_animal_ctor", (method_id_t)animal_ctor, (impl_t)animal_default_ctor},
        {"yanimal_animal_dtor", (method_id_t)animal_dtor, (impl_t)animal_default_dtor},
        {"yanimal_animal_breathe", (method_id_t)animal_breathe, (impl_t)animal_default_breathe},
        {"yanimal_animal_speak", (method_id_t)animal_speak, (impl_t)animal_default_speak},
        {"yanimal_animal_eat", (method_id_t)animal_eat, (impl_t)animal_default_eat},
    };
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         NULL, NULL, 0);
    return cls;
}
