/* tuned_sportscar — subclass living in a DIFFERENT module from its
 * parent. ytuning inherits from yvehicle:sportscar and overrides a slot
 * owned by yvehicle. This is the proof that struct op's slot_domain,
 * the packed (domain_id, idx) method_slot, and class_inherit_dispatch
 * actually compose into a working cross-domain vtable. */

#include "class.h"

#include <stdio.h>

struct [[clang::annotate("class@ytuning:tuned_sportscar")]]
       [[clang::annotate("parent@yvehicle:sportscar")]] tuned_sportscar_data {
    int boost_level;
};

[[clang::annotate("override@ytuning:tuned_sportscar:yvehicle:vehicle_describe")]]
static struct str tuned_sportscar_describe(struct ctx *ctx, struct object *obj,
                                           float distance)
{
    (void)ctx;
    struct str r;
    snprintf(r.buf, sizeof(r.buf),
             "TUNED sportscar@%p super-describe(distance=%.1f)",
             (void *)obj, distance);
    fprintf(stderr, "  [impl] tuned_sportscar_describe -> '%s'\n", r.buf);
    return r;
}

#include "tuned_sportscar.gen.c"
