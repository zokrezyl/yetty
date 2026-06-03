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

struct class_ptr_result yanimal_animal_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YETTY_OK(class_ptr, cls);
    ydebug("registering class=yanimal_animal");

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
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(class_ptr, "yanimal_animal_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}

struct yanimal_animal_data_ptr_result yanimal_animal_data_get(struct object *obj)
{
    if (!obj) {
        return YETTY_ERR(yanimal_animal_data_ptr, "yanimal_animal_data_get: NULL object");
    }
    struct class_ptr_result class_result = yanimal_animal_class_get();
    YETTY_RETURN_IF_ERR(yanimal_animal_data_ptr, class_result, "yanimal_animal_data_get: class accessor failed");
    struct yetty_ycore_size_result offset_result =
        object_data_offset(object_class(obj), class_result.value);
    YETTY_RETURN_IF_ERR(yanimal_animal_data_ptr, offset_result, "yanimal_animal_data_get: object_data_offset failed");
    return YETTY_OK(yanimal_animal_data_ptr, (struct animal_data *)((char *)obj + offset_result.value));
}

struct yetty_ycore_int_result yanimal_animal_age_get(struct object *obj)
{
    struct yanimal_animal_data_ptr_result data = yanimal_animal_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data, "yanimal_animal_age_get: data block");
    return YETTY_OK(yetty_ycore_int, data.value->age);
}

struct yetty_ycore_void_result yanimal_animal_age_set(struct object *obj, int value)
{
    struct yanimal_animal_data_ptr_result data = yanimal_animal_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "yanimal_animal_age_set: data block");
    data.value->age = value;
    return YETTY_OK_VOID();
}

struct yetty_ycore_int_result yanimal_animal_energy_get(struct object *obj)
{
    struct yanimal_animal_data_ptr_result data = yanimal_animal_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data, "yanimal_animal_energy_get: data block");
    return YETTY_OK(yetty_ycore_int, data.value->energy);
}

struct yetty_ycore_void_result yanimal_animal_energy_set(struct object *obj, int value)
{
    struct yanimal_animal_data_ptr_result data = yanimal_animal_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "yanimal_animal_energy_set: data block");
    data.value->energy = value;
    return YETTY_OK_VOID();
}
