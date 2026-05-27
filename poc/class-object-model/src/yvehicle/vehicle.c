/* Vehicle — base class default impls. */

#include "class.h"

#include <stdio.h>

struct [[clang::annotate("class@yvehicle:vehicle")]] vehicle_data {
    int mileage;
};

[[clang::annotate("override@yvehicle:vehicle:vehicle_ctor")]]
static void vehicle_default_ctor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    fprintf(stderr, "  [impl] vehicle_default_ctor(obj=%p)\n", (void *)obj);
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_dtor")]]
static void vehicle_default_dtor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    fprintf(stderr, "  [impl] vehicle_default_dtor(obj=%p)\n", (void *)obj);
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_start")]]
static void vehicle_default_start(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    fprintf(stderr, "  [impl] vehicle_default_start(obj=%p)\n", (void *)obj);
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_accelerate")]]
static int vehicle_default_accelerate(struct ctx *ctx, struct object *obj, float speed)
{
    (void)ctx;
    fprintf(stderr, "  [impl] vehicle_default_accelerate(obj=%p, speed=%.1f)\n",
            (void *)obj, speed);
    return 0;
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_brake")]]
static int vehicle_default_brake(struct ctx *ctx, struct object *obj, float intensity)
{
    (void)ctx;
    fprintf(stderr, "  [impl] vehicle_default_brake(obj=%p, intensity=%.1f)\n",
            (void *)obj, intensity);
    return 0;
}

[[clang::annotate("override@yvehicle:vehicle:vehicle_describe")]]
static struct str vehicle_default_describe(struct ctx *ctx, struct object *obj, float distance)
{
    (void)ctx;
    struct str r;
    snprintf(r.buf, sizeof(r.buf),
             "vehicle@%p describe(distance=%.1f)",
             (void *)obj, distance);
    fprintf(stderr, "  [impl] vehicle_default_describe -> '%s'\n", r.buf);
    return r;
}

#include "vehicle.gen.c"
