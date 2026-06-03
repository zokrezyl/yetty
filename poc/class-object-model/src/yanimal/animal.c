/* Animal — base class default impls. Operates on its own members (age,
 * energy) through the data handle; subclasses reach them via the generated
 * getters/setters. */

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
    struct animal_data *self = yanimal_animal_data(obj);
    self->age = 0;
    self->energy = 100;
    ydebug("obj=%p age=%d energy=%d", (void *)obj, self->age, self->energy);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yanimal:animal:animal_dtor")]]
static struct yetty_ycore_void_result animal_default_dtor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct animal_data *self = yanimal_animal_data(obj);
    ydebug("obj=%p final energy=%d", (void *)obj, self->energy);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yanimal:animal:animal_breathe")]]
static struct yetty_ycore_void_result animal_default_breathe(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    struct animal_data *self = yanimal_animal_data(obj);
    self->energy += 5;
    if (self->energy > 100) {
        self->energy = 100;
    }
    ydebug("obj=%p energy=%d", (void *)obj, self->energy);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yanimal:animal:animal_speak")]]
static struct str_result animal_default_speak(struct ctx *ctx, struct object *obj, int volume)
{
    (void)ctx;
    struct animal_data *self = yanimal_animal_data(obj);
    self->energy -= 1;
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "animal@%p generic sound, energy=%d (volume=%d)", (void *)obj,
             self->energy, volume);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

[[clang::annotate("override@yanimal:animal:animal_eat")]]
static struct yetty_ycore_int_result animal_default_eat(struct ctx *ctx, struct object *obj,
                                                        float amount)
{
    (void)ctx;
    struct animal_data *self = yanimal_animal_data(obj);
    self->energy += (int)(amount * 20.0f);
    if (self->energy > 100) {
        self->energy = 100;
    }
    ydebug("obj=%p amount=%.1fkg energy=%d", (void *)obj, amount, self->energy);
    return YETTY_OK(yetty_ycore_int, self->energy);
}

#include "animal.gen.c"
