/* GENERATED — do not edit. */
#include "yvehicle/car.h"
#include "yvehicle/electric.h"
#include "yvehicle/sportscar.h"

__attribute__((
    unused)) static yvehicle_vehicle_ctor_fn _yvehicle_sportscar_yvehicle_vehicle_ctor_check =
    sportscar_ctor;
__attribute__((unused)) static yvehicle_vehicle_accelerate_fn
    _yvehicle_sportscar_yvehicle_vehicle_accelerate_check = sportscar_accelerate;
__attribute__((unused)) static yvehicle_vehicle_describe_fn
    _yvehicle_sportscar_yvehicle_vehicle_describe_check = sportscar_describe;

struct class_ptr_result yvehicle_sportscar_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) {
        return YETTY_OK(class_ptr, cls);
    }
    ydebug("registering class=yvehicle_sportscar");

    static const struct class_descriptor desc = {
        .name = "yvehicle_sportscar",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct sportscar_data),
    };
    static const struct op ops[] = {
        {"yvehicle", "vehicle_ctor", (method_id_t)yvehicle_vehicle_ctor, (impl_t)sportscar_ctor},
        {"yvehicle", "vehicle_accelerate", (method_id_t)yvehicle_vehicle_accelerate,
         (impl_t)sportscar_accelerate},
        {"yvehicle", "vehicle_describe", (method_id_t)yvehicle_vehicle_describe,
         (impl_t)sportscar_describe},
    };
    struct class_ptr_result _parent_r = yvehicle_car_class_get();
    if (YETTY_IS_ERR(_parent_r)) {
        return YETTY_ERR(class_ptr, "yvehicle_sportscar_class_get: parent accessor failed",
                         _parent_r);
    }
    struct class_ptr_result _mixin0_r = yvehicle_electric_mixin_get();
    if (YETTY_IS_ERR(_mixin0_r)) {
        return YETTY_ERR(class_ptr, "yvehicle_sportscar_class_get: mixin0 accessor failed",
                         _mixin0_r);
    }
    const struct class *mixins[] = {_mixin0_r.value};
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]), _parent_r.value, mixins, 1);
    if (YETTY_IS_ERR(_r)) {
        return YETTY_ERR(class_ptr, "yvehicle_sportscar_class_get: class_register failed", _r);
    }
    cls = _r.value;
    return _r;
}

struct yvehicle_sportscar_data_ptr_result yvehicle_sportscar_data_get(struct object *obj)
{
    if (!obj) {
        return YETTY_ERR(yvehicle_sportscar_data_ptr, "yvehicle_sportscar_data_get: NULL object");
    }
    struct class_ptr_result class_result = yvehicle_sportscar_class_get();
    YETTY_RETURN_IF_ERR(yvehicle_sportscar_data_ptr, class_result,
                        "yvehicle_sportscar_data_get: class accessor failed");
    struct yetty_ycore_size_result offset_result =
        object_data_offset(object_class(obj), class_result.value);
    YETTY_RETURN_IF_ERR(yvehicle_sportscar_data_ptr, offset_result,
                        "yvehicle_sportscar_data_get: object_data_offset failed");
    return YETTY_OK(yvehicle_sportscar_data_ptr,
                    (struct sportscar_data *)((char *)obj + offset_result.value));
}

struct yetty_ycore_int_result yvehicle_sportscar_top_speed_get(struct object *obj)
{
    struct yvehicle_sportscar_data_ptr_result data = yvehicle_sportscar_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data, "yvehicle_sportscar_top_speed_get: data block");
    return YETTY_OK(yetty_ycore_int, data.value->top_speed);
}

struct yetty_ycore_int_result yvehicle_sportscar_turbo_engaged_get(struct object *obj)
{
    struct yvehicle_sportscar_data_ptr_result data = yvehicle_sportscar_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data, "yvehicle_sportscar_turbo_engaged_get: data block");
    return YETTY_OK(yetty_ycore_int, data.value->turbo_engaged);
}

struct yetty_ycore_void_result yvehicle_sportscar_turbo_engaged_set(struct object *obj, int value)
{
    struct yvehicle_sportscar_data_ptr_result data = yvehicle_sportscar_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "yvehicle_sportscar_turbo_engaged_set: data block");
    data.value->turbo_engaged = value;
    return YETTY_OK_VOID();
}
