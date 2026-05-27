/* GENERATED — do not edit. */
#include "yanimal/animal.h"
#include "yanimal/cat.h"
#include "yanimal/pet.h"

const struct class *yanimal_cat_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return cls;

    static const struct class_descriptor desc = {
        .name = "yanimal_cat",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct cat_data),
    };
    static const struct op ops[] = {
        {"yanimal", "animal_speak", (method_id_t)yanimal_animal_speak, (impl_t)cat_speak},
    };
    const struct class *mixins[] = { yanimal_pet_mixin_get() };
    cls = class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                         yanimal_animal_class_get(), mixins, 1);
    return cls;
}
