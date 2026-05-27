/* GENERATED — do not edit. */
#include "yanimal/pet.h"

__attribute__((unused))
static yanimal_animal_eat_fn _yanimal_pet_yanimal_animal_eat_check = pet_eat;

const struct class *yanimal_pet_mixin_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {
        .name = "yanimal_pet",
        .type = CLASS_TYPE_MIXIN,
        .data_size = sizeof(struct pet_data),
    };
    static const struct op ops[] = {
        {"yanimal", "animal_eat", (method_id_t)yanimal_animal_eat, (impl_t)pet_eat},
    };
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         NULL, NULL, 0);
    return cls;
}
