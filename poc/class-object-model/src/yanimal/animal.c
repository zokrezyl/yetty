/* Animal — base class default impls. */

#include "class.h"

#include <stdio.h>

struct [[clang::annotate("class:animal")]] animal_data {
    int age;
};

[[clang::annotate("override:animal:animal_ctor")]]
static void animal_default_ctor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    fprintf(stderr, "  [impl] animal_default_ctor(obj=%p)\n", (void *)obj);
}

[[clang::annotate("override:animal:animal_dtor")]]
static void animal_default_dtor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    fprintf(stderr, "  [impl] animal_default_dtor(obj=%p)\n", (void *)obj);
}

[[clang::annotate("override:animal:animal_breathe")]]
static void animal_default_breathe(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    fprintf(stderr, "  [impl] animal_default_breathe(obj=%p)\n", (void *)obj);
}

[[clang::annotate("override:animal:animal_speak")]]
static struct str animal_default_speak(struct ctx *ctx, struct object *obj, int volume)
{
    (void)ctx;
    struct str r;
    snprintf(r.buf, sizeof(r.buf),
             "animal@%p makes a generic sound (volume=%d)",
             (void *)obj, volume);
    fprintf(stderr, "  [impl] animal_default_speak -> '%s'\n", r.buf);
    return r;
}

[[clang::annotate("override:animal:animal_eat")]]
static int animal_default_eat(struct ctx *ctx, struct object *obj, float amount)
{
    (void)ctx;
    fprintf(stderr, "  [impl] animal_default_eat(obj=%p, amount=%.1fkg)\n",
            (void *)obj, amount);
    return 0;
}

#include "animal.gen.c"
