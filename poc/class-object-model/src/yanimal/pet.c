/* Pet mixin — overrides animal_eat with portion control.
 *
 * Reaches its OWN member (treats_today) through the data handle, and the
 * host animal's `energy` through animal's generated getter/setter. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include "yanimal/pet.h"    /* own handle + treats accessors */
#include "yanimal/animal.h" /* host animal's energy accessors */

#include <stdio.h>

struct [[clang::annotate("mixin@yanimal:pet")]] pet_data {
    [[clang::annotate("property")]] int treats_today;
};

[[clang::annotate("override@yanimal:pet:animal_eat")]]
static struct yetty_ycore_int_result pet_eat(struct ctx *ctx, struct object *obj, float amount)
{
    (void)ctx;
    struct pet_data *self = yanimal_pet_data(obj); /* own (mixin) */
    self->treats_today += 1;

    /* portion control: at most 0.5kg counts toward energy */
    float effective = amount > 0.5f ? 0.5f : amount;
    int energy = yanimal_animal_energy_get(obj) + (int)(effective * 20.0f); /* host */
    if (energy > 100) {
        energy = 100;
    }
    yanimal_animal_energy_set(obj, energy);

    ydebug("portion-controlled: %.1fkg requested, treats=%d energy=%d", amount, self->treats_today,
           energy);
    return YETTY_OK(yetty_ycore_int, self->treats_today);
}

#include "pet.gen.c"
