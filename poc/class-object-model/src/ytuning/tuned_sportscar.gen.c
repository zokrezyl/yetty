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

struct tuned_sportscar_data *ytuning_tuned_sportscar_data(struct object *obj)
{
    if (!obj) {
        ydebug("ytuning_tuned_sportscar_data: NULL object");
        return NULL;
    }
    struct class_ptr_result class_result = ytuning_tuned_sportscar_class_get();
    if (YETTY_IS_ERR(class_result)) {
        yetty_ycore_error_print(stderr, "ytuning_tuned_sportscar_data", class_result.error);
        yetty_ycore_error_destroy(class_result.error);
        return NULL;
    }
    struct yetty_ycore_size_result offset_result =
        object_data_offset(object_class(obj), class_result.value);
    if (YETTY_IS_ERR(offset_result)) {
        yetty_ycore_error_print(stderr, "ytuning_tuned_sportscar_data", offset_result.error);
        yetty_ycore_error_destroy(offset_result.error);
        return NULL;
    }
    return (struct tuned_sportscar_data *)((char *)obj + offset_result.value);
}

int ytuning_tuned_sportscar_boost_level_get(struct object *obj)
{
    struct tuned_sportscar_data *data = ytuning_tuned_sportscar_data(obj);
    if (!data) {
        ydebug("ytuning_tuned_sportscar_boost_level_get: no data block for obj=%p", (void *)obj);
        int fallback = {0};
        return fallback;
    }
    return data->boost_level;
}

void ytuning_tuned_sportscar_boost_level_set(struct object *obj, int value)
{
    struct tuned_sportscar_data *data = ytuning_tuned_sportscar_data(obj);
    if (!data) {
        ydebug("ytuning_tuned_sportscar_boost_level_set: no data block for obj=%p", (void *)obj);
        return;
    }
    data->boost_level = value;
}
