/* GENERATED — do not edit. */
#include "yvehicle/electric.h"

__attribute__((unused))
static yvehicle_vehicle_brake_fn _yvehicle_electric_yvehicle_vehicle_brake_check = electric_brake;

struct class_ptr_result yvehicle_electric_mixin_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YETTY_OK(class_ptr, cls);
    ydebug("registering class=yvehicle_electric");

    static const struct class_descriptor desc = {
        .name = "yvehicle_electric",
        .type = CLASS_TYPE_MIXIN,
        .data_size = sizeof(struct electric_data),
    };
    static const struct op ops[] = {
        {"yvehicle", "vehicle_brake", (method_id_t)yvehicle_vehicle_brake, (impl_t)electric_brake},
    };
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       NULL, NULL, 0);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(class_ptr, "yvehicle_electric_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}
