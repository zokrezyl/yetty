/* Sportscar — car subclass, uses electric mixin. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include <stdio.h>

struct [[clang::annotate("class@yvehicle:sportscar")]]
       [[clang::annotate("parent@yvehicle:car")]]
       [[clang::annotate("uses@yvehicle:electric")]] sportscar_data {
    int top_speed;
};

[[clang::annotate("override@yvehicle:sportscar:vehicle_describe")]]
static struct str_result sportscar_describe(struct ctx *ctx, struct object *obj, float distance)
{
    (void)ctx;
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "sportscar@%p describe(distance=%.1f)", (void *)obj, distance);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

[[clang::annotate("override@yvehicle:sportscar:vehicle_accelerate")]]
static struct yetty_ycore_int_result sportscar_accelerate(struct ctx *ctx, struct object *obj,
                                                          float speed)
{
    (void)ctx;
    ydebug("turbo! obj=%p speed=%.1f", (void *)obj, speed);
    return YETTY_OK(yetty_ycore_int, 1);
}

#include "sportscar.gen.c"
