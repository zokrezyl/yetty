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

struct electric_data *yvehicle_electric_data(struct object *obj)
{
    if (!obj) {
        ydebug("yvehicle_electric_data: NULL object");
        return NULL;
    }
    struct class_ptr_result class_result = yvehicle_electric_mixin_get();
    if (YETTY_IS_ERR(class_result)) {
        yetty_ycore_error_print(stderr, "yvehicle_electric_data", class_result.error);
        yetty_ycore_error_destroy(class_result.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_result =
        object_data_offset(object_class(obj), class_result.value);
    if (YETTY_IS_ERR(offset_result)) {
        yetty_ycore_error_print(stderr, "yvehicle_electric_data", offset_result.error);
        yetty_ycore_error_destroy(offset_result.error);
        return NULL;
    }
    return (struct electric_data *)((char *)obj + offset_result.value);
}

int yvehicle_electric_battery_percent_get(struct object *obj)
{
    struct electric_data *data = yvehicle_electric_data(obj);
    if (!data) {
        ydebug("yvehicle_electric_battery_percent_get: no data block for obj=%p", (void *)obj);
        int fallback = {0};
        return fallback;
    }
    return data->battery_percent;
}

void yvehicle_electric_battery_percent_set(struct object *obj, int value)
{
    struct electric_data *data = yvehicle_electric_data(obj);
    if (!data) {
        ydebug("yvehicle_electric_battery_percent_set: no data block for obj=%p", (void *)obj);
        return;
    }
    data->battery_percent = value;
}
