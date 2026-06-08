/* Sportscar — car subclass, uses the electric mixin.
 *
 * The showcase. A single subclass works with FOUR data blocks of one
 * object, each through the right surface, every accessor returning a Result:
 *   - its own members (top_speed, turbo_engaged) via the data handle
 *   - vehicle's members (mileage, speed)  via yvehicle_vehicle_*_{get,set}
 *   - car's member (doors)                via yvehicle_car_doors_{get,set}
 *   - electric's member (battery_percent) via yvehicle_electric_battery_*
 * It never sees any of those structs — only the generated accessors.
 *
 * top_speed is `property:ro`: a getter is generated but no setter, so the
 * owner (here) sets it directly through the handle while other classes can
 * only read it. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include "yvehicle/sportscar.h" /* own handle + car/vehicle/electric accessors */

#include <stdio.h>

struct [[clang::annotate("class@yvehicle:sportscar")]] [[clang::annotate(
    "parent@yvehicle:car")]] [[clang::annotate("uses@yvehicle:electric")]] sportscar_data {
    [[clang::annotate("property:ro")]] int top_speed; /* read-only to others */
    [[clang::annotate("property")]] int turbo_engaged;
};

[[clang::annotate("override@yvehicle:sportscar:vehicle_ctor")]]
static struct yetty_ycore_void_result sportscar_ctor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct yvehicle_sportscar_data_ptr_result self = yvehicle_sportscar_data_get(obj); /* own */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, self, "sportscar_ctor: data block");
    self.value->top_speed = 320;
    self.value->turbo_engaged = 0;

    struct yetty_ycore_void_result wr;
    wr = yvehicle_vehicle_mileage_set(obj, 1200); /* grandparent */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "sportscar_ctor: set mileage");
    wr = yvehicle_vehicle_speed_set(obj, 0);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "sportscar_ctor: set speed");
    wr = yvehicle_car_doors_set(obj, 2); /* parent */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "sportscar_ctor: set doors");
    wr = yvehicle_electric_battery_percent_set(obj, 100); /* mixin */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "sportscar_ctor: set battery");

    ydebug("obj=%p init top_speed=%d", (void *)obj, self.value->top_speed);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yvehicle:sportscar:vehicle_accelerate")]]
static struct yetty_ycore_int_result sportscar_accelerate(struct ctx *ctx, struct object *obj,
                                                          float speed)
{
    (void)ctx;
    struct yvehicle_sportscar_data_ptr_result self = yvehicle_sportscar_data_get(obj); /* own */
    YETTY_RETURN_IF_ERR(yetty_ycore_int, self, "sportscar_accelerate: data block");
    self.value->turbo_engaged = 1;

    int gain = (int)speed * 2; /* turbo doubles the requested gain */
    struct yetty_ycore_int_result cur = yvehicle_vehicle_speed_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, cur, "sportscar_accelerate: speed get");
    int new_speed = cur.value + gain;
    if (new_speed > self.value->top_speed) {
        new_speed = self.value->top_speed;
    }
    struct yetty_ycore_void_result wr = yvehicle_vehicle_speed_set(obj, new_speed);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, wr, "sportscar_accelerate: speed set");

    struct yetty_ycore_int_result mileage = yvehicle_vehicle_mileage_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, mileage, "sportscar_accelerate: mileage get");
    wr = yvehicle_vehicle_mileage_set(obj, mileage.value + gain / 5);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, wr, "sportscar_accelerate: mileage set");

    struct yetty_ycore_int_result battery = yvehicle_electric_battery_percent_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, battery, "sportscar_accelerate: battery get");
    int drained = battery.value - 5;
    if (drained < 0) {
        drained = 0;
    }
    wr = yvehicle_electric_battery_percent_set(obj, drained);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, wr, "sportscar_accelerate: battery set");

    ydebug("turbo! obj=%p speed=%d/%d battery=%d%%", (void *)obj, new_speed, self.value->top_speed,
           drained);
    return YETTY_OK(yetty_ycore_int, new_speed);
}

[[clang::annotate("override@yvehicle:sportscar:vehicle_describe")]]
static struct str_result sportscar_describe(struct ctx *ctx, struct object *obj, float distance)
{
    (void)ctx;
    struct yvehicle_sportscar_data_ptr_result self = yvehicle_sportscar_data_get(obj); /* own */
    YETTY_RETURN_IF_ERR(str, self, "sportscar_describe: data block");
    struct yetty_ycore_int_result speed = yvehicle_vehicle_speed_get(obj);
    YETTY_RETURN_IF_ERR(str, speed, "sportscar_describe: speed");
    struct yetty_ycore_int_result mileage = yvehicle_vehicle_mileage_get(obj);
    YETTY_RETURN_IF_ERR(str, mileage, "sportscar_describe: mileage");
    struct yetty_ycore_int_result doors = yvehicle_car_doors_get(obj);
    YETTY_RETURN_IF_ERR(str, doors, "sportscar_describe: doors");
    struct yetty_ycore_int_result battery = yvehicle_electric_battery_percent_get(obj);
    YETTY_RETURN_IF_ERR(str, battery, "sportscar_describe: battery");

    struct str r;
    snprintf(
        r.buf, sizeof(r.buf),
        "sportscar@%p %d/%d km/h, %d doors, mileage=%d, battery=%d%%, turbo=%s (distance=%.1f)",
        (void *)obj, speed.value, self.value->top_speed, doors.value, mileage.value, battery.value,
        self.value->turbo_engaged ? "on" : "off", distance);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "sportscar.gen.c"
