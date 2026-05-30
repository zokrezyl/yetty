/* Pet mixin — overrides animal_eat with portion control. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include <stdio.h>

struct [[clang::annotate("mixin@yanimal:pet")]] pet_data {
    int treats_today;
};

[[clang::annotate("override@yanimal:pet:animal_eat")]]
static struct yetty_ycore_int_result pet_eat(struct ctx *ctx, struct object *obj, float amount)
{
    (void)ctx;
    (void)obj;
    ydebug("portion-controlled: %.1fkg requested, capping at 0.5kg", amount);
    return YETTY_OK(yetty_ycore_int, amount > 0.5f ? 1 : 0);
}

#include "pet.gen.c"
