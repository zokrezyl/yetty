/* GENERATED — do not edit. */
#include "yanimal/animal.h"

__attribute__((unused))
static yanimal_animal_ctor_fn _yanimal_animal_yanimal_animal_ctor_check = animal_default_ctor;
__attribute__((unused))
static yanimal_animal_dtor_fn _yanimal_animal_yanimal_animal_dtor_check = animal_default_dtor;
__attribute__((unused))
static yanimal_animal_breathe_fn _yanimal_animal_yanimal_animal_breathe_check = animal_default_breathe;
__attribute__((unused))
static yanimal_animal_speak_fn _yanimal_animal_yanimal_animal_speak_check = animal_default_speak;
__attribute__((unused))
static yanimal_animal_eat_fn _yanimal_animal_yanimal_animal_eat_check = animal_default_eat;

const struct class *yanimal_animal_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {
        .name = "yanimal_animal",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct animal_data),
    };
    static const struct op ops[] = {
        {"yanimal", "animal_ctor", (method_id_t)yanimal_animal_ctor, (impl_t)animal_default_ctor},
        {"yanimal", "animal_dtor", (method_id_t)yanimal_animal_dtor, (impl_t)animal_default_dtor},
        {"yanimal", "animal_breathe", (method_id_t)yanimal_animal_breathe, (impl_t)animal_default_breathe},
        {"yanimal", "animal_speak", (method_id_t)yanimal_animal_speak, (impl_t)animal_default_speak},
        {"yanimal", "animal_eat", (method_id_t)yanimal_animal_eat, (impl_t)animal_default_eat},
    };
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         NULL, NULL, 0);
    return cls;
}
