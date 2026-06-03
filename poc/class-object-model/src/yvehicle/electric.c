/* Electric mixin — overrides vehicle_brake with regenerative behaviour.
 *
 * Reaches its OWN member (battery_percent) through the data handle, and the
 * host vehicle's `speed` through vehicle's generated setter/getter. A mixin
 * has no parent annotation, but the slot it overrides belongs to
 * yvehicle:vehicle, so it pulls vehicle's public header for the accessors. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include "yvehicle/electric.h" /* own handle + battery accessors */
#include "yvehicle/vehicle.h"  /* host vehicle's speed accessors */

#include <stdio.h>

struct [[clang::annotate("mixin@yvehicle:electric")]] electric_data {
    [[clang::annotate("property")]] int battery_percent;
};

[[clang::annotate("override@yvehicle:electric:vehicle_brake")]]
static struct yetty_ycore_int_result electric_brake(struct ctx *ctx, struct object *obj,
                                                    float intensity)
{
    (void)ctx;
    struct electric_data *self = yvehicle_electric_data(obj); /* own (mixin) */

    self->battery_percent += (int)(intensity * 10.0f); /* regen */
    if (self->battery_percent > 100) {
        self->battery_percent = 100;
    }

    int speed = yvehicle_vehicle_speed_get(obj); /* host class */
    speed -= (int)(speed * intensity);
    if (speed < 0) {
        speed = 0;
    }
    yvehicle_vehicle_speed_set(obj, speed);

    ydebug("regenerative intensity=%.1f battery=%d%% speed=%d", intensity, self->battery_percent,
           speed);
    return YETTY_OK(yetty_ycore_int, self->battery_percent);
}

#include "electric.gen.c"
