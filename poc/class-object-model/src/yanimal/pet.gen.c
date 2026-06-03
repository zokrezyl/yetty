/* GENERATED — do not edit. */
#include "yanimal/pet.h"

__attribute__((unused))
static yanimal_animal_eat_fn _yanimal_pet_yanimal_animal_eat_check = pet_eat;

struct class_ptr_result yanimal_pet_mixin_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YETTY_OK(class_ptr, cls);
    ydebug("registering class=yanimal_pet");

    static const struct class_descriptor desc = {
        .name = "yanimal_pet",
        .type = CLASS_TYPE_MIXIN,
        .data_size = sizeof(struct pet_data),
    };
    static const struct op ops[] = {
        {"yanimal", "animal_eat", (method_id_t)yanimal_animal_eat, (impl_t)pet_eat},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(class_ptr, "yanimal_pet_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}

struct pet_data *yanimal_pet_data(struct object *obj)
{
    if (!obj) {
        ydebug("yanimal_pet_data: NULL object");
        return NULL;
    }
    struct class_ptr_result class_result = yanimal_pet_mixin_get();
    if (YETTY_IS_ERR(class_result)) {
        yetty_ycore_error_print(stderr, "yanimal_pet_data", class_result.error);
        yetty_ycore_error_destroy(class_result.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_result =
        object_data_offset(object_class(obj), class_result.value);
    if (YETTY_IS_ERR(offset_result)) {
        yetty_ycore_error_print(stderr, "yanimal_pet_data", offset_result.error);
        yetty_ycore_error_destroy(offset_result.error);
        return NULL;
    }
    return (struct pet_data *)((char *)obj + offset_result.value);
}

int yanimal_pet_treats_today_get(struct object *obj)
{
    struct pet_data *data = yanimal_pet_data(obj);
    if (!data) {
        ydebug("yanimal_pet_treats_today_get: no data block for obj=%p", (void *)obj);
        int fallback = {0};
        return fallback;
    }
    return data->treats_today;
}

void yanimal_pet_treats_today_set(struct object *obj, int value)
{
    struct pet_data *data = yanimal_pet_data(obj);
    if (!data) {
        ydebug("yanimal_pet_treats_today_set: no data block for obj=%p", (void *)obj);
        return;
    }
    data->treats_today = value;
}
