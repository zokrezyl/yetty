/* GENERATED — do not edit. */
#include "yanimal/animal.h"
#include "yanimal/dog.h"
#include "yanimal/pet.h"

__attribute__((unused))
static yanimal_animal_ctor_fn _yanimal_dog_yanimal_animal_ctor_check = dog_ctor;
__attribute__((unused))
static yanimal_animal_speak_fn _yanimal_dog_yanimal_animal_speak_check = dog_speak;

struct class_ptr_result yanimal_dog_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YETTY_OK(class_ptr, cls);
    ydebug("registering class=yanimal_dog");

    static const struct class_descriptor desc = {
        .name = "yanimal_dog",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct dog_data),
    };
    static const struct op ops[] = {
        {"yanimal", "animal_ctor", (method_id_t)yanimal_animal_ctor, (impl_t)dog_ctor},
        {"yanimal", "animal_speak", (method_id_t)yanimal_animal_speak, (impl_t)dog_speak},
    };
    struct class_ptr_result _parent_r = yanimal_animal_class_get();
    if (YETTY_IS_ERR(_parent_r))
        return YETTY_ERR(class_ptr, "yanimal_dog_class_get: parent accessor failed", _parent_r);
    struct class_ptr_result _mixin0_r = yanimal_pet_mixin_get();
    if (YETTY_IS_ERR(_mixin0_r))
        return YETTY_ERR(class_ptr, "yanimal_dog_class_get: mixin0 accessor failed", _mixin0_r);
    const struct class *mixins[] = { _mixin0_r.value };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       _parent_r.value, mixins, 1);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(class_ptr, "yanimal_dog_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}

struct dog_data *yanimal_dog_data(struct object *obj)
{
    if (!obj) {
        ydebug("yanimal_dog_data: NULL object");
        return NULL;
    }
    struct class_ptr_result class_result = yanimal_dog_class_get();
    if (YETTY_IS_ERR(class_result)) {
        yetty_ycore_error_print(stderr, "yanimal_dog_data", class_result.error);
        yetty_ycore_error_destroy(class_result.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_result =
        object_data_offset(object_class(obj), class_result.value);
    if (YETTY_IS_ERR(offset_result)) {
        yetty_ycore_error_print(stderr, "yanimal_dog_data", offset_result.error);
        yetty_ycore_error_destroy(offset_result.error);
        return NULL;
    }
    return (struct dog_data *)((char *)obj + offset_result.value);
}

int yanimal_dog_loyalty_get(struct object *obj)
{
    struct dog_data *data = yanimal_dog_data(obj);
    if (!data) {
        ydebug("yanimal_dog_loyalty_get: no data block for obj=%p", (void *)obj);
        int fallback = {0};
        return fallback;
    }
    return data->loyalty;
}

void yanimal_dog_loyalty_set(struct object *obj, int value)
{
    struct dog_data *data = yanimal_dog_data(obj);
    if (!data) {
        ydebug("yanimal_dog_loyalty_set: no data block for obj=%p", (void *)obj);
        return;
    }
    data->loyalty = value;
}
