/* Cat — animal subclass, uses the pet mixin. Reaches its own member
 * (lives_remaining) via the data handle, and the parent (animal) + mixin
 * (pet) members via their generated getters/setters. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include "yanimal/cat.h" /* own handle + animal/pet accessors */

#include <stdio.h>

struct [[clang::annotate("class@yanimal:cat")]]
       [[clang::annotate("parent@yanimal:animal")]]
       [[clang::annotate("uses@yanimal:pet")]] cat_data {
    [[clang::annotate("property")]] int lives_remaining;
};

[[clang::annotate("override@yanimal:cat:animal_ctor")]]
static struct yetty_ycore_void_result cat_ctor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct cat_data *self = yanimal_cat_data(obj); /* own */
    self->lives_remaining = 9;

    yanimal_animal_age_set(obj, 3); /* parent */
    yanimal_animal_energy_set(obj, 80);
    yanimal_pet_treats_today_set(obj, 0); /* mixin */

    ydebug("obj=%p lives=%d age=%d energy=%d", (void *)obj, self->lives_remaining,
           yanimal_animal_age_get(obj), yanimal_animal_energy_get(obj));
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yanimal:cat:animal_speak")]]
static struct str_result cat_speak(struct ctx *ctx, struct object *obj, int volume)
{
    (void)ctx;
    struct cat_data *self = yanimal_cat_data(obj); /* own */
    yanimal_animal_energy_set(obj, yanimal_animal_energy_get(obj) - 1);
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "cat@%p: meow! energy=%d lives=%d treats=%d (volume=%d)",
             (void *)obj, yanimal_animal_energy_get(obj), self->lives_remaining,
             yanimal_pet_treats_today_get(obj), volume);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "cat.gen.c"
