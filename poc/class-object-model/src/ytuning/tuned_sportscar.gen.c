/* GENERATED — do not edit. */
#include "ytuning/tuned_sportscar.h"
#include "yvehicle/methods.gen.h"
#include "yvehicle/sportscar.h"

__attribute__((unused))
static yvehicle_vehicle_start_fn _ytuning_tuned_sportscar_yvehicle_vehicle_start_check = tuned_sportscar_start;
__attribute__((unused))
static yvehicle_vehicle_describe_fn _ytuning_tuned_sportscar_yvehicle_vehicle_describe_check = tuned_sportscar_describe;

struct class_ptr_result ytuning_tuned_sportscar_class_get(void)
{
    static const struct class *cls = NULL;
    if (cls) return YETTY_OK(class_ptr, cls);
    ydebug("registering class=ytuning_tuned_sportscar");

    static const struct class_descriptor desc = {
        .name = "ytuning_tuned_sportscar",
        .type = CLASS_TYPE_REGULAR,
        .data_size = sizeof(struct tuned_sportscar_data),
    };
    static const struct op ops[] = {
        {"yvehicle", "vehicle_start", (method_id_t)yvehicle_vehicle_start, (impl_t)tuned_sportscar_start},
        {"yvehicle", "vehicle_describe", (method_id_t)yvehicle_vehicle_describe, (impl_t)tuned_sportscar_describe},
    };
    struct class_ptr_result _parent_r = yvehicle_sportscar_class_get();
    if (YETTY_IS_ERR(_parent_r))
        return YETTY_ERR(class_ptr, "ytuning_tuned_sportscar_class_get: parent accessor failed", _parent_r);
    struct class_ptr_result _r =
        class_register(&desc, ops, sizeof(ops) / sizeof(ops[0]),
                       _parent_r.value, NULL, 0);
    if (YETTY_IS_ERR(_r))
        return YETTY_ERR(class_ptr, "ytuning_tuned_sportscar_class_get: class_register failed", _r);
    cls = _r.value;
    return _r;
}

struct ytuning_tuned_sportscar_data_ptr_result ytuning_tuned_sportscar_data_get(struct object *obj)
{
    if (!obj) {
        return YETTY_ERR(ytuning_tuned_sportscar_data_ptr, "ytuning_tuned_sportscar_data_get: NULL object");
    }
    struct class_ptr_result class_result = ytuning_tuned_sportscar_class_get();
    YETTY_RETURN_IF_ERR(ytuning_tuned_sportscar_data_ptr, class_result, "ytuning_tuned_sportscar_data_get: class accessor failed");
    struct yetty_ycore_size_result offset_result =
        object_data_offset(object_class(obj), class_result.value);
    YETTY_RETURN_IF_ERR(ytuning_tuned_sportscar_data_ptr, offset_result, "ytuning_tuned_sportscar_data_get: object_data_offset failed");
    return YETTY_OK(ytuning_tuned_sportscar_data_ptr, (struct tuned_sportscar_data *)((char *)obj + offset_result.value));
}

struct yetty_ycore_int_result ytuning_tuned_sportscar_boost_level_get(struct object *obj)
{
    struct ytuning_tuned_sportscar_data_ptr_result data = ytuning_tuned_sportscar_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, data, "ytuning_tuned_sportscar_boost_level_get: data block");
    return YETTY_OK(yetty_ycore_int, data.value->boost_level);
}

struct yetty_ycore_void_result ytuning_tuned_sportscar_boost_level_set(struct object *obj, int value)
{
    struct ytuning_tuned_sportscar_data_ptr_result data = ytuning_tuned_sportscar_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, data, "ytuning_tuned_sportscar_boost_level_set: data block");
    data.value->boost_level = value;
    return YETTY_OK_VOID();
}
