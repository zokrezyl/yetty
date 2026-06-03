/* Motorbike — vehicle subclass. Own member via the data handle, inherited
 * vehicle members via vehicle's getters. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include "yvehicle/motorbike.h"

#include <stdio.h>

struct [[clang::annotate("class@yvehicle:motorbike")]]
       [[clang::annotate("parent@yvehicle:vehicle")]] motorbike_data {
    [[clang::annotate("property")]] int has_sidecar;
};

[[clang::annotate("override@yvehicle:motorbike:vehicle_describe")]]
static struct str_result motorbike_describe(struct ctx *ctx, struct object *obj, float distance)
{
    (void)ctx;
    struct motorbike_data *self = yvehicle_motorbike_data(obj); /* own */
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "motorbike@%p sidecar=%s mileage=%d speed=%d (distance=%.1f)",
             (void *)obj, self->has_sidecar ? "yes" : "no", yvehicle_vehicle_mileage_get(obj),
             yvehicle_vehicle_speed_get(obj), distance);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "motorbike.gen.c"
