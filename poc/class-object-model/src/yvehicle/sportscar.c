/* Sportscar — car subclass, uses electric mixin. */

#include "class.h"

#include <stdio.h>

struct [[clang::annotate("class@yvehicle:sportscar")]]
       [[clang::annotate("parent@yvehicle:car")]]
       [[clang::annotate("uses@yvehicle:electric")]] sportscar_data {
    int top_speed;
};

[[clang::annotate("override@yvehicle:sportscar:vehicle_describe")]]
static struct str sportscar_describe(struct ctx *ctx, struct object *obj, float distance)
{
    (void)ctx;
    struct str r;
    snprintf(r.buf, sizeof(r.buf),
             "sportscar@%p describe(distance=%.1f)",
             (void *)obj, distance);
    fprintf(stderr, "  [impl] sportscar_describe -> '%s'\n", r.buf);
    return r;
}

[[clang::annotate("override@yvehicle:sportscar:vehicle_accelerate")]]
static int sportscar_accelerate(struct ctx *ctx, struct object *obj, float speed)
{
    (void)ctx;
    fprintf(stderr, "  [impl] sportscar_accelerate(obj=%p, speed=%.1f) — turbo!\n",
            (void *)obj, speed);
    return 1;
}

#include "sportscar.gen.c"
