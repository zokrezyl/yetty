/* GENERATED — do not edit. */
#include "ytuning/tuned_sportscar.h"
#include "yvehicle/methods.gen.h"
#include "yvehicle/sportscar.h"

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
