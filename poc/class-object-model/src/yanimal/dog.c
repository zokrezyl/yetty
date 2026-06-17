/* Dog — animal subclass, uses the pet mixin. Own member (loyalty) via the
 * data handle; parent (animal) + mixin (pet) members via their accessors —
 * all Result-returning. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include "yanimal/dog.h"

#include <stdio.h>

struct [[clang::annotate("class@yanimal:dog")]] [[clang::annotate(
    "parent@yanimal:animal")]] [[clang::annotate("uses@yanimal:pet")]] dog_data {
    [[clang::annotate("property")]] int loyalty;
};

[[clang::annotate("override@yanimal:dog:animal_ctor")]]
static struct yetty_ycore_void_result dog_ctor(struct object *obj)
{
    struct yanimal_dog_data_ptr_result self = yanimal_dog_data_get(obj); /* own */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, self, "dog_ctor: data block");
    self.value->loyalty = 100;

    struct yetty_ycore_void_result wr;
    wr = yanimal_animal_age_set(obj, 5); /* parent */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "dog_ctor: set age");
    wr = yanimal_animal_energy_set(obj, 90);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "dog_ctor: set energy");
    wr = yanimal_pet_treats_today_set(obj, 0); /* mixin */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "dog_ctor: set treats");

    ydebug("obj=%p loyalty=%d", (void *)obj, self.value->loyalty);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yanimal:dog:animal_speak")]]
static struct str_result dog_speak(struct object *obj, int volume)
{
    struct yanimal_dog_data_ptr_result self = yanimal_dog_data_get(obj); /* own */
    YETTY_RETURN_IF_ERR(str, self, "dog_speak: data block");
    struct yetty_ycore_int_result energy = yanimal_animal_energy_get(obj); /* parent */
    YETTY_RETURN_IF_ERR(str, energy, "dog_speak: energy get");
    struct yetty_ycore_void_result wr = yanimal_animal_energy_set(obj, energy.value - 1);
    YETTY_RETURN_IF_ERR(str, wr, "dog_speak: energy set");
    struct yetty_ycore_int_result treats = yanimal_pet_treats_today_get(obj); /* mixin */
    YETTY_RETURN_IF_ERR(str, treats, "dog_speak: treats get");

    struct str r;
    snprintf(r.buf, sizeof(r.buf), "dog@%p: woof! energy=%d loyalty=%d treats=%d (volume=%d)",
             (void *)obj, energy.value - 1, self.value->loyalty, treats.value, volume);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "dog.gen.c"
