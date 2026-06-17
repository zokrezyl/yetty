/* Pet mixin — overrides animal_eat with portion control.
 *
 * Reaches its OWN member (treats_today) through the data handle, and the
 * host animal's `energy` through animal's generated getter/setter. Every
 * accessor returns a Result, propagated with YETTY_RETURN_IF_ERR. */

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
static struct yetty_ycore_int_result pet_eat(struct object *obj, float amount)
{
    struct yanimal_pet_data_ptr_result self = yanimal_pet_data_get(obj); /* own */
    YETTY_RETURN_IF_ERR(yetty_ycore_int, self, "pet_eat: data block");
    self.value->treats_today += 1;

    /* portion control: at most 0.5kg counts toward energy */
    float effective = amount > 0.5f ? 0.5f : amount;
    struct yetty_ycore_int_result energy = yanimal_animal_energy_get(obj); /* host */
    YETTY_RETURN_IF_ERR(yetty_ycore_int, energy, "pet_eat: energy get");
    int fed = energy.value + (int)(effective * 20.0f);
    if (fed > 100) {
        fed = 100;
    }
    struct yetty_ycore_void_result wr = yanimal_animal_energy_set(obj, fed);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, wr, "pet_eat: energy set");

    ydebug("portion-controlled: %.1fkg requested, treats=%d energy=%d", amount,
           self.value->treats_today, fed);
    return YETTY_OK(yetty_ycore_int, self.value->treats_today);
}

#include "pet.gen.c"
