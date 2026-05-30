/* Vehicle — base class default impls. Every impl returns a Result. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include <stdio.h>

struct [[clang::annotate("class@yvehicle:vehicle")]] vehicle_data {
    int mileage;
};

[[clang::annotate("override@yvehicle:vehicle:vehicle_ctor")]]
static struct yetty_ycore_void_result vehicle_default_ctor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    ydebug("obj=%p", (void *)obj);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_dtor")]]
static struct yetty_ycore_void_result vehicle_default_dtor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    ydebug("obj=%p", (void *)obj);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_start")]]
static struct yetty_ycore_void_result vehicle_default_start(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    ydebug("obj=%p", (void *)obj);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_accelerate")]]
static struct yetty_ycore_int_result vehicle_default_accelerate(struct ctx *ctx, struct object *obj,
                                                                float speed)
{
    (void)ctx;
    ydebug("obj=%p speed=%.1f", (void *)obj, speed);
    return YETTY_OK(yetty_ycore_int, 0);
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_brake")]]
static struct yetty_ycore_int_result vehicle_default_brake(struct ctx *ctx, struct object *obj,
                                                           float intensity)
{
    (void)ctx;
    ydebug("obj=%p intensity=%.1f", (void *)obj, intensity);
    return YETTY_OK(yetty_ycore_int, 0);
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_describe")]]
static struct str_result vehicle_default_describe(struct ctx *ctx, struct object *obj,
                                                  float distance)
{
    (void)ctx;
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "vehicle@%p describe(distance=%.1f)", (void *)obj, distance);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "vehicle.gen.c"
