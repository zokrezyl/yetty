/* Animal — base class default impls. Operates on its own members (age,
 * energy) through the data handle, which returns a Result; subclasses reach
 * them via the generated Result-returning getters/setters. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include "yanimal/animal.h"

#include <stdio.h>

struct [[clang::annotate("class@yanimal:animal")]] animal_data {
    [[clang::annotate("property")]] int age;
    [[clang::annotate("property")]] int energy;
};

[[clang::annotate("override@yanimal:animal:animal_ctor")]]
static struct yetty_ycore_void_result animal_default_ctor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct yanimal_animal_data_ptr_result self = yanimal_animal_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, self, "animal_default_ctor: data block");
    self.value->age = 0;
    self.value->energy = 100;
    ydebug("obj=%p age=%d energy=%d", (void *)obj, self.value->age, self.value->energy);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yanimal:animal:animal_dtor")]]
static struct yetty_ycore_void_result animal_default_dtor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct yanimal_animal_data_ptr_result self = yanimal_animal_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, self, "animal_default_dtor: data block");
    ydebug("obj=%p final energy=%d", (void *)obj, self.value->energy);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yanimal:animal:animal_breathe")]]
static struct yetty_ycore_void_result animal_default_breathe(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct yanimal_animal_data_ptr_result self = yanimal_animal_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, self, "animal_default_breathe: data block");
    self.value->energy += 5;
    if (self.value->energy > 100) {
        self.value->energy = 100;
    }
    ydebug("obj=%p energy=%d", (void *)obj, self.value->energy);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yanimal:animal:animal_speak")]]
static struct str_result animal_default_speak(struct ctx *ctx, struct object *obj, int volume)
{
    (void)ctx;
    struct yanimal_animal_data_ptr_result self = yanimal_animal_data_get(obj);
    YETTY_RETURN_IF_ERR(str, self, "animal_default_speak: data block");
    self.value->energy -= 1;
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "animal@%p generic sound, energy=%d (volume=%d)", (void *)obj,
             self.value->energy, volume);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

[[clang::annotate("override@yanimal:animal:animal_eat")]]
static struct yetty_ycore_int_result animal_default_eat(struct ctx *ctx, struct object *obj,
                                                        float amount)
{
    (void)ctx;
    struct yanimal_animal_data_ptr_result self = yanimal_animal_data_get(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, self, "animal_default_eat: data block");
    self.value->energy += (int)(amount * 20.0f);
    if (self.value->energy > 100) {
        self.value->energy = 100;
    }
    ydebug("obj=%p amount=%.1fkg energy=%d", (void *)obj, amount, self.value->energy);
    return YETTY_OK(yetty_ycore_int, self.value->energy);
}

#include "animal.gen.c"
