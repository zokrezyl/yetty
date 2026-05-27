/* Electric mixin — overrides vehicle_brake with regenerative behaviour. */

#include "class.h"

#include <stdio.h>

struct [[clang::annotate("mixin@yvehicle:electric")]] electric_data {
    int battery_percent;
};

[[clang::annotate("override@yvehicle:electric:vehicle_brake")]]
static int electric_brake(struct ctx *ctx, struct object *obj, float intensity)
{
    (void)ctx;
    (void)obj;
    printf("electric_brake (regenerative): intensity=%.1f\n", intensity);
    return 1;
}

#include "electric.gen.c"
