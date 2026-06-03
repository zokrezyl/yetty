/* Sportscar — car subclass, uses the electric mixin.
 *
 * The showcase. A single subclass works with FOUR data blocks of one
 * object, each through the right surface:
 *   - its own members (top_speed, turbo_engaged) via the data handle
 *   - vehicle's members (mileage, speed)  via yvehicle_vehicle_*_{get,set}
 *   - car's member (doors)                via yvehicle_car_doors_{get,set}
 *   - electric's member (battery_percent) via yvehicle_electric_battery_*
 * It never sees any of those structs — only the generated accessors.
 *
 * top_speed is annotated `property:ro`: a getter is generated but no
 * setter, so the owner (here) sets it directly through the handle while
 * other classes can only read it. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include "yvehicle/sportscar.h" /* own handle + car/vehicle/electric accessors */

#include <stdio.h>

struct [[clang::annotate("class@yvehicle:sportscar")]]
       [[clang::annotate("parent@yvehicle:car")]]
       [[clang::annotate("uses@yvehicle:electric")]] sportscar_data {
    [[clang::annotate("property:ro")]] int top_speed; /* read-only to others */
    [[clang::annotate("property")]] int turbo_engaged;
};

[[clang::annotate("override@yvehicle:sportscar:vehicle_ctor")]]
static struct yetty_ycore_void_result sportscar_ctor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct sportscar_data *self = yvehicle_sportscar_data(obj); /* own */
    self->top_speed = 320;
    self->turbo_engaged = 0;

    yvehicle_vehicle_mileage_set(obj, 1200); /* grandparent */
    yvehicle_vehicle_speed_set(obj, 0);
    yvehicle_car_doors_set(obj, 2);              /* parent */
    yvehicle_electric_battery_percent_set(obj, 100); /* mixin */

    ydebug("obj=%p init top_speed=%d doors=%d battery=%d%%", (void *)obj, self->top_speed,
           yvehicle_car_doors_get(obj), yvehicle_electric_battery_percent_get(obj));
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yvehicle:sportscar:vehicle_accelerate")]]
static struct yetty_ycore_int_result sportscar_accelerate(struct ctx *ctx, struct object *obj,
                                                          float speed)
{
    (void)ctx;
    struct sportscar_data *self = yvehicle_sportscar_data(obj); /* own */
    self->turbo_engaged = 1;

    int gain = (int)speed * 2; /* turbo doubles the requested gain */
    int new_speed = yvehicle_vehicle_speed_get(obj) + gain;
    if (new_speed > self->top_speed) {
        new_speed = self->top_speed;
    }
    yvehicle_vehicle_speed_set(obj, new_speed);
    yvehicle_vehicle_mileage_set(obj, yvehicle_vehicle_mileage_get(obj) + gain / 5);

    int battery = yvehicle_electric_battery_percent_get(obj) - 5;
    if (battery < 0) {
        battery = 0;
    }
    yvehicle_electric_battery_percent_set(obj, battery);

    ydebug("turbo! obj=%p speed=%d/%d battery=%d%%", (void *)obj, new_speed, self->top_speed,
           battery);
    return YETTY_OK(yetty_ycore_int, new_speed);
}

[[clang::annotate("override@yvehicle:sportscar:vehicle_describe")]]
static struct str_result sportscar_describe(struct ctx *ctx, struct object *obj, float distance)
{
    (void)ctx;
    struct sportscar_data *self = yvehicle_sportscar_data(obj); /* own */
    struct str r;
    snprintf(r.buf, sizeof(r.buf),
             "sportscar@%p %d/%d km/h, %d doors, mileage=%d, battery=%d%%, turbo=%s (distance=%.1f)",
             (void *)obj, yvehicle_vehicle_speed_get(obj), self->top_speed,
             yvehicle_car_doors_get(obj), yvehicle_vehicle_mileage_get(obj),
             yvehicle_electric_battery_percent_get(obj), self->turbo_engaged ? "on" : "off",
             distance);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "sportscar.gen.c"
