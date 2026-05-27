/* GENERATED — do not edit. */
#include "yanimal/animal.h"
#include "yanimal/cat.h"
#include "yanimal/pet.h"

const struct class *cat_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {
        .name = "cat",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct cat_data),
    };
    static const struct op ops[] = {
        {"animal_speak", (method_id_t)animal_speak, (impl_t)cat_speak},
    };
    const struct class *mixins[] = { pet_mixin_get() };
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         animal_class_get(), mixins, 1);
    return cls;
}
