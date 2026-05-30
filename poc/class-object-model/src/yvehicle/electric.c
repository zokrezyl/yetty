/* Electric mixin — overrides vehicle_brake with regenerative behaviour. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include <stdio.h>

struct [[clang::annotate("mixin@yvehicle:electric")]] electric_data {
    int battery_percent;
};

[[clang::annotate("override@yvehicle:electric:vehicle_brake")]]
static struct yetty_ycore_int_result electric_brake(struct ctx *ctx, struct object *obj,
                                                    float intensity)
{
    (void)ctx;
    (void)obj;
    ydebug("regenerative intensity=%.1f", intensity);
    return YETTY_OK(yetty_ycore_int, 1);
}

#include "electric.gen.c"
