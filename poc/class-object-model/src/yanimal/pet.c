/* Pet mixin — overrides animal_eat with portion control. */

#include "class.h"

#include <stdio.h>

struct [[clang::annotate("mixin:pet")]] pet_data {
    int treats_today;
};

[[clang::annotate("override:pet:animal_eat")]]
static int pet_eat(struct ctx *ctx, struct object *obj, float amount)
{
    (void)ctx;
    (void)obj;
    printf("pet_eat (portion-controlled): %.1fkg requested, capping at 0.5kg\n",
           amount);
    return amount > 0.5f ? 1 : 0;
}

#include "pet.gen.c"
