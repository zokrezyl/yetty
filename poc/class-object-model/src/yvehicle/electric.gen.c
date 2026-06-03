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

struct yvehicle_electric_data_ptr_result yvehicle_electric_data_get(struct object *obj)
{
    if (!obj) {
        return YETTY_ERR(yvehicle_electric_data_ptr, "yvehicle_electric_data_get: NULL object");
    }
    struct class_ptr_result class_result = yvehicle_electric_mixin_get();
    YETTY_RETURN_IF_ERR(yvehicle_electric_data_ptr, class_result, "yvehicle_electric_data_get: class accessor failed");
    struct yetty_ycore_size_result offset_result =
        object_data_offset(object_class(obj), class_result.value);
    YETTY_RETURN_IF_ERR(yvehicle_electric_data_ptr, offset_result, "yvehicle_electric_data_get: object_data_offset failed");
    return YETTY_OK(yvehicle_electric_data_ptr, (struct electric_data *)((char *)obj + offset_result.value));
}

struct yetty_ycore_int_result yvehicle_electric_battery_percent_get(struct object *obj)
{
    struct yvehicle_electric_data_ptr_result data = yvehicle_electric_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data, "yvehicle_electric_battery_percent_get: data block");
    return YETTY_OK(yetty_ycore_int, data.value->battery_percent);
}

struct yetty_ycore_void_result yvehicle_electric_battery_percent_set(struct object *obj, int value)
{
    struct yvehicle_electric_data_ptr_result data = yvehicle_electric_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "yvehicle_electric_battery_percent_set: data block");
    data.value->battery_percent = value;
    return YETTY_OK_VOID();
}
