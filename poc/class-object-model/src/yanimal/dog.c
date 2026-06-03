/* Dog — animal subclass, uses the pet mixin. Own member (loyalty) via the
 * data handle; parent (animal) + mixin (pet) members via their accessors. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include "yanimal/dog.h"

#include <stdio.h>

struct [[clang::annotate("class@yanimal:dog")]]
       [[clang::annotate("parent@yanimal:animal")]]
       [[clang::annotate("uses@yanimal:pet")]] dog_data {
    [[clang::annotate("property")]] int loyalty;
};

[[clang::annotate("override@yanimal:dog:animal_ctor")]]
static struct yetty_ycore_void_result dog_ctor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct dog_data *self = yanimal_dog_data(obj); /* own */
    self->loyalty = 100;

    yanimal_animal_age_set(obj, 5); /* parent */
    yanimal_animal_energy_set(obj, 90);
    yanimal_pet_treats_today_set(obj, 0); /* mixin */

    ydebug("obj=%p loyalty=%d age=%d energy=%d", (void *)obj, self->loyalty,
           yanimal_animal_age_get(obj), yanimal_animal_energy_get(obj));
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yanimal:dog:animal_speak")]]
static struct str_result dog_speak(struct ctx *ctx, struct object *obj, int volume)
{
    (void)ctx;
    struct dog_data *self = yanimal_dog_data(obj); /* own */
    yanimal_animal_energy_set(obj, yanimal_animal_energy_get(obj) - 1);
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "dog@%p: woof! energy=%d loyalty=%d treats=%d (volume=%d)",
             (void *)obj, yanimal_animal_energy_get(obj), self->loyalty,
             yanimal_pet_treats_today_get(obj), volume);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "dog.gen.c"
