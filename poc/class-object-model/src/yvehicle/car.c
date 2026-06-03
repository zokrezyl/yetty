/* Car — vehicle subclass. Reaches its OWN member (doors) through the data
 * handle, and the inherited vehicle members through vehicle's generated
 * getters. It can never see vehicle's private fuel_level. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include "yvehicle/car.h" /* car handle + vehicle accessors (via parent header) */

#include <stdio.h>

struct [[clang::annotate("class@yvehicle:car")]]
       [[clang::annotate("parent@yvehicle:vehicle")]] car_data {
    [[clang::annotate("property")]] int doors;
};

[[clang::annotate("override@yvehicle:car:vehicle_describe")]]
static struct str_result car_describe(struct ctx *ctx, struct object *obj, float distance)
{
    (void)ctx;
    struct car_data *self = yvehicle_car_data(obj); /* own */
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "car@%p doors=%d mileage=%d speed=%d (distance=%.1f)",
             (void *)obj, self->doors, yvehicle_vehicle_mileage_get(obj),
             yvehicle_vehicle_speed_get(obj), distance);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "car.gen.c"
