/* Motorbike — vehicle subclass. Own member via the data handle, inherited
 * vehicle members via vehicle's getters; all Result-returning. */

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
    struct yvehicle_motorbike_data_ptr_result self = yvehicle_motorbike_data_get(obj); /* own */
    YETTY_RETURN_IF_ERR(str, self, "motorbike_describe: data block");
    struct yetty_ycore_int_result mileage = yvehicle_vehicle_mileage_get(obj); /* parent */
    YETTY_RETURN_IF_ERR(str, mileage, "motorbike_describe: mileage");
    struct yetty_ycore_int_result speed = yvehicle_vehicle_speed_get(obj); /* parent */
    YETTY_RETURN_IF_ERR(str, speed, "motorbike_describe: speed");

    struct str r;
    snprintf(r.buf, sizeof(r.buf), "motorbike@%p sidecar=%s mileage=%d speed=%d (distance=%.1f)",
             (void *)obj, self.value->has_sidecar ? "yes" : "no", mileage.value, speed.value,
             distance);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "motorbike.gen.c"
