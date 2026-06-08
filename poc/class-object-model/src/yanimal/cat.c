/* Cat — animal subclass, uses the pet mixin. Reaches its own member
 * (lives_remaining) via the data handle, and the parent (animal) + mixin
 * (pet) members via their generated getters/setters — all Result-returning. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include "yanimal/cat.h" /* own handle + animal/pet accessors */

#include <stdio.h>

struct [[clang::annotate("class@yanimal:cat")]] [[clang::annotate(
    "parent@yanimal:animal")]] [[clang::annotate("uses@yanimal:pet")]] cat_data {
    [[clang::annotate("property")]] int lives_remaining;
};

[[clang::annotate("override@yanimal:cat:animal_ctor")]]
static struct yetty_ycore_void_result cat_ctor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct yanimal_cat_data_ptr_result self = yanimal_cat_data_get(obj); /* own */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, self, "cat_ctor: data block");
    self.value->lives_remaining = 9;

    struct yetty_ycore_void_result wr;
    wr = yanimal_animal_age_set(obj, 3); /* parent */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "cat_ctor: set age");
    wr = yanimal_animal_energy_set(obj, 80);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "cat_ctor: set energy");
    wr = yanimal_pet_treats_today_set(obj, 0); /* mixin */
    YETTY_RETURN_IF_ERR(yetty_ycore_void, wr, "cat_ctor: set treats");

    ydebug("obj=%p lives=%d", (void *)obj, self.value->lives_remaining);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yanimal:cat:animal_speak")]]
static struct str_result cat_speak(struct ctx *ctx, struct object *obj, int volume)
{
    (void)ctx;
    struct yanimal_cat_data_ptr_result self = yanimal_cat_data_get(obj); /* own */
    YETTY_RETURN_IF_ERR(str, self, "cat_speak: data block");
    struct yetty_ycore_int_result energy = yanimal_animal_energy_get(obj); /* parent */
    YETTY_RETURN_IF_ERR(str, energy, "cat_speak: energy get");
    struct yetty_ycore_void_result wr = yanimal_animal_energy_set(obj, energy.value - 1);
    YETTY_RETURN_IF_ERR(str, wr, "cat_speak: energy set");
    struct yetty_ycore_int_result treats = yanimal_pet_treats_today_get(obj); /* mixin */
    YETTY_RETURN_IF_ERR(str, treats, "cat_speak: treats get");

    struct str r;
    snprintf(r.buf, sizeof(r.buf), "cat@%p: meow! energy=%d lives=%d treats=%d (volume=%d)",
             (void *)obj, energy.value - 1, self.value->lives_remaining, treats.value, volume);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "cat.gen.c"
