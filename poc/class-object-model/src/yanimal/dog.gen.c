/* GENERATED — do not edit. */
#include "yanimal/animal.h"
#include "yanimal/dog.h"
#include "yanimal/pet.h"

const struct class *dog_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {
        .name = "yanimal_dog",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct dog_data),
    };
    static const struct op ops[] = {
        {"yanimal_animal_speak", (method_id_t)animal_speak, (impl_t)dog_speak},
    };
    const struct class *mixins[] = { pet_mixin_get() };
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         animal_class_get(), mixins, 1);
    return cls;
}
