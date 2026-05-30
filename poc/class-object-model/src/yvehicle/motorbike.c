/* Motorbike — vehicle subclass. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include <stdio.h>

struct [[clang::annotate("class@yvehicle:motorbike")]]
       [[clang::annotate("parent@yvehicle:vehicle")]] motorbike_data {
    int has_sidecar;
};

[[clang::annotate("override@yvehicle:motorbike:vehicle_describe")]]
static struct str_result motorbike_describe(struct ctx *ctx, struct object *obj, float distance)
{
    (void)ctx;
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "motorbike@%p describe(distance=%.1f)", (void *)obj, distance);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "motorbike.gen.c"
