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

struct animal_data *yanimal_animal_data(struct object *obj)
{
    if (!obj) {
        ydebug("yanimal_animal_data: NULL object");
        return NULL;
    }
    struct class_ptr_result class_result = yanimal_animal_class_get();
    if (YETTY_IS_ERR(class_result)) {
        yetty_ycore_error_print(stderr, "yanimal_animal_data", class_result.error);
        yetty_ycore_error_destroy(class_result.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_result =
        object_data_offset(object_class(obj), class_result.value);
    if (YETTY_IS_ERR(offset_result)) {
        yetty_ycore_error_print(stderr, "yanimal_animal_data", offset_result.error);
        yetty_ycore_error_destroy(offset_result.error);
        return NULL;
    }
    return (struct animal_data *)((char *)obj + offset_result.value);
}

int yanimal_animal_age_get(struct object *obj)
{
    struct animal_data *data = yanimal_animal_data(obj);
    if (!data) {
        ydebug("yanimal_animal_age_get: no data block for obj=%p", (void *)obj);
        int fallback = {0};
        return fallback;
    }
    return data->age;
}

void yanimal_animal_age_set(struct object *obj, int value)
{
    struct animal_data *data = yanimal_animal_data(obj);
    if (!data) {
        ydebug("yanimal_animal_age_set: no data block for obj=%p", (void *)obj);
        return;
    }
    data->age = value;
}

int yanimal_animal_energy_get(struct object *obj)
{
    struct animal_data *data = yanimal_animal_data(obj);
    if (!data) {
        ydebug("yanimal_animal_energy_get: no data block for obj=%p", (void *)obj);
        int fallback = {0};
        return fallback;
    }
    return data->energy;
}

void yanimal_animal_energy_set(struct object *obj, int value)
{
    struct animal_data *data = yanimal_animal_data(obj);
    if (!data) {
        ydebug("yanimal_animal_energy_set: no data block for obj=%p", (void *)obj);
        return;
    }
    data->energy = value;
}
