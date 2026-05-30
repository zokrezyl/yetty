/* GENERATED — do not edit. */
#include "yvehicle/car.h"
#include "yvehicle/electric.h"
#include "yvehicle/sportscar.h"

__attribute__((unused))
static yvehicle_vehicle_describe_fn _yvehicle_sportscar_yvehicle_vehicle_describe_check = sportscar_describe;
__attribute__((unused))
static yvehicle_vehicle_accelerate_fn _yvehicle_sportscar_yvehicle_vehicle_accelerate_check = sportscar_accelerate;

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
        {"yvehicle", "vehicle_describe", (method_id_t)yvehicle_vehicle_describe, (impl_t)sportscar_describe},
        {"yvehicle", "vehicle_accelerate", (method_id_t)yvehicle_vehicle_accelerate, (impl_t)sportscar_accelerate},
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
