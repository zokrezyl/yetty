/* GENERATED — do not edit. */
#include "yvehicle/car.h"
#include "yvehicle/electric.h"
#include "yvehicle/sportscar.h"

__attribute__((unused))
static yvehicle_vehicle_ctor_fn _yvehicle_sportscar_yvehicle_vehicle_ctor_check = sportscar_ctor;
__attribute__((unused))
static yvehicle_vehicle_accelerate_fn _yvehicle_sportscar_yvehicle_vehicle_accelerate_check = sportscar_accelerate;
__attribute__((unused))
static yvehicle_vehicle_describe_fn _yvehicle_sportscar_yvehicle_vehicle_describe_check = sportscar_describe;

struct class_ptr_result yvehicle_sportscar_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YETTY_OK(class_ptr, cls);
    ydebug("registering class=yvehicle_sportscar");

    static const struct class_descriptor desc = {
        .name = "yvehicle_sportscar",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct sportscar_data),
    };
    static const struct op ops[] = {
        {"yvehicle", "vehicle_ctor", (method_id_t)yvehicle_vehicle_ctor, (impl_t)sportscar_ctor},
        {"yvehicle", "vehicle_accelerate", (method_id_t)yvehicle_vehicle_accelerate, (impl_t)sportscar_accelerate},
        {"yvehicle", "vehicle_describe", (method_id_t)yvehicle_vehicle_describe, (impl_t)sportscar_describe},
    };
    struct class_ptr_result _parent_r = yvehicle_car_class_get();
    if (YETTY_IS_ERR(_parent_r))
        return YETTY_ERR(class_ptr, "yvehicle_sportscar_class_get: parent accessor failed", _parent_r);
    struct class_ptr_result _mixin0_r = yvehicle_electric_mixin_get();
    if (YETTY_IS_ERR(_mixin0_r))
        return YETTY_ERR(class_ptr, "yvehicle_sportscar_class_get: mixin0 accessor failed", _mixin0_r);
    const struct class *mixins[] = { _mixin0_r.value };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       _parent_r.value, mixins, 1);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(class_ptr, "yvehicle_sportscar_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}

struct sportscar_data *yvehicle_sportscar_data(struct object *obj)
{
    if (!obj) {
        ydebug("yvehicle_sportscar_data: NULL object");
        return NULL;
    }
    struct class_ptr_result class_result = yvehicle_sportscar_class_get();
    if (YETTY_IS_ERR(class_result)) {
        yetty_ycore_error_print(stderr, "yvehicle_sportscar_data", class_result.error);
        yetty_ycore_error_destroy(class_result.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_result =
        object_data_offset(object_class(obj), class_result.value);
    if (YETTY_IS_ERR(offset_result)) {
        yetty_ycore_error_print(stderr, "yvehicle_sportscar_data", offset_result.error);
        yetty_ycore_error_destroy(offset_result.error);
        return NULL;
    }
    return (struct sportscar_data *)((char *)obj + offset_result.value);
}

int yvehicle_sportscar_top_speed_get(struct object *obj)
{
    struct sportscar_data *data = yvehicle_sportscar_data(obj);
    if (!data) {
        ydebug("yvehicle_sportscar_top_speed_get: no data block for obj=%p", (void *)obj);
        int fallback = {0};
        return fallback;
    }
    return data->top_speed;
}

int yvehicle_sportscar_turbo_engaged_get(struct object *obj)
{
    struct sportscar_data *data = yvehicle_sportscar_data(obj);
    if (!data) {
        ydebug("yvehicle_sportscar_turbo_engaged_get: no data block for obj=%p", (void *)obj);
        int fallback = {0};
        return fallback;
    }
    return data->turbo_engaged;
}

void yvehicle_sportscar_turbo_engaged_set(struct object *obj, int value)
{
    struct sportscar_data *data = yvehicle_sportscar_data(obj);
    if (!data) {
        ydebug("yvehicle_sportscar_turbo_engaged_set: no data block for obj=%p", (void *)obj);
        return;
    }
    data->turbo_engaged = value;
}
