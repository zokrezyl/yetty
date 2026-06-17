/* Electric mixin — overrides vehicle_brake with regenerative behaviour.
 *
 * Reaches its OWN member (battery_percent) through the data handle, and the
 * host vehicle's `speed` through vehicle's generated getter/setter. A mixin
 * has no parent annotation, but the slot it overrides belongs to
 * yvehicle:vehicle, so it pulls vehicle's public header for the accessors.
 * Every accessor returns a Result, propagated with YETTY_RETURN_IF_ERR. */

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
static struct yetty_ycore_int_result electric_brake(struct object *obj, float intensity)
{
    struct yvehicle_electric_data_ptr_result self = yvehicle_electric_data_get(obj); /* own */
    YETTY_RETURN_IF_ERR(yetty_ycore_int, self, "electric_brake: data block");

    self.value->battery_percent += (int)(intensity * 10.0f); /* regen */
    if (self.value->battery_percent > 100) {
        self.value->battery_percent = 100;
    }

    struct yetty_ycore_int_result speed = yvehicle_vehicle_speed_get(obj); /* host */
    YETTY_RETURN_IF_ERR(yetty_ycore_int, speed, "electric_brake: speed get");
    int slowed = speed.value - (int)(speed.value * intensity);
    if (slowed < 0) {
        slowed = 0;
    }
    struct yetty_ycore_void_result wr = yvehicle_vehicle_speed_set(obj, slowed);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, wr, "electric_brake: speed set");

    ydebug("regenerative intensity=%.1f battery=%d%% speed=%d", intensity,
           self.value->battery_percent, slowed);
    return YETTY_OK(yetty_ycore_int, self.value->battery_percent);
}

#include "electric.gen.c"
