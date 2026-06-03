/* tuned_sportscar — subclass living in a DIFFERENT module from its parent.
 * ytuning inherits from yvehicle:sportscar and overrides slots owned by
 * yvehicle. Besides the cross-domain vtable, this shows CROSS-MODULE data
 * access: tuned_sportscar reads and writes yvehicle's members purely through
 * the generated (Result-returning) getters/setters — it never sees a
 * yvehicle struct. Its own boost_level is reached through its own handle.
 *
 * Read-only boundary: yvehicle:sportscar's top_speed is `property:ro`, so
 * `yvehicle_sportscar_top_speed_get` exists but there is no `..._set`.
 * tuned_sportscar reads it but cannot set it — and deliberately does NOT
 * override the constructor, so it inherits sportscar's full initialisation
 * (including that read-only top_speed it could not set itself). It engages
 * its own boost in an overridden `start` instead. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include "ytuning/tuned_sportscar.h" /* own handle + the yvehicle accessors */

#include <stdio.h>

struct [[clang::annotate("class@ytuning:tuned_sportscar")]]
       [[clang::annotate("parent@yvehicle:sportscar")]] tuned_sportscar_data {
    [[clang::annotate("property")]] int boost_level;
};

[[clang::annotate("override@ytuning:tuned_sportscar:yvehicle:vehicle_start")]]
static struct yetty_ycore_void_result tuned_sportscar_start(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct ytuning_tuned_sportscar_data_ptr_result self = ytuning_tuned_sportscar_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, self, "tuned_sportscar_start: data block");
    self.value->boost_level = 3;

    struct yetty_ycore_void_result wr = yvehicle_vehicle_speed_set(obj, 0); /* cross-module write */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "tuned_sportscar_start: speed set");

    ydebug("obj=%p engaged boost=%d", (void *)obj, self.value->boost_level);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@ytuning:tuned_sportscar:yvehicle:vehicle_describe")]]
static struct str_result tuned_sportscar_describe(struct ctx *ctx, struct object *obj,
                                                  float distance)
{
    (void)ctx;
    struct ytuning_tuned_sportscar_data_ptr_result self = ytuning_tuned_sportscar_data_get(obj);
    YETTY_RETURN_IF_ERR(str, self, "tuned_sportscar_describe: data block");
    /* Cross-module reads of yvehicle members, including the read-only
     * top_speed (getter only — there is no setter to call). */
    struct yetty_ycore_int_result speed = yvehicle_vehicle_speed_get(obj);
    YETTY_RETURN_IF_ERR(str, speed, "tuned_sportscar_describe: speed");
    struct yetty_ycore_int_result top_speed = yvehicle_sportscar_top_speed_get(obj);
    YETTY_RETURN_IF_ERR(str, top_speed, "tuned_sportscar_describe: top_speed");
    struct yetty_ycore_int_result doors = yvehicle_car_doors_get(obj);
    YETTY_RETURN_IF_ERR(str, doors, "tuned_sportscar_describe: doors");
    struct yetty_ycore_int_result mileage = yvehicle_vehicle_mileage_get(obj);
    YETTY_RETURN_IF_ERR(str, mileage, "tuned_sportscar_describe: mileage");
    struct yetty_ycore_int_result battery = yvehicle_electric_battery_percent_get(obj);
    YETTY_RETURN_IF_ERR(str, battery, "tuned_sportscar_describe: battery");

    struct str r;
    snprintf(r.buf, sizeof(r.buf),
             "TUNED sportscar@%p %d/%d km/h boost=%d doors=%d mileage=%d battery=%d%% "
             "(distance=%.1f)",
             (void *)obj, speed.value, top_speed.value, self.value->boost_level, doors.value,
             mileage.value, battery.value, distance);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "tuned_sportscar.gen.c"
