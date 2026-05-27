/* Car — vehicle subclass. */

#include "class.h"

#include <stdio.h>

struct [[clang::annotate("class@yvehicle:car")]]
       [[clang::annotate("parent@yvehicle:vehicle")]] car_data {
    int doors;
};

[[clang::annotate("override@yvehicle:car:vehicle_describe")]]
static struct str car_describe(struct ctx *ctx, struct object *obj, float distance)
{
    (void)ctx;
    struct str r;
    snprintf(r.buf, sizeof(r.buf),
             "car@%p describe(distance=%.1f)",
             (void *)obj, distance);
    fprintf(stderr, "  [impl] car_describe -> '%s'\n", r.buf);
    return r;
}

#include "car.gen.c"
