/* Animal — base class default impls. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include <stdio.h>

struct [[clang::annotate("class@yanimal:animal")]] animal_data {
    int age;
};

[[clang::annotate("override@yanimal:animal:animal_ctor")]]
static struct yetty_ycore_void_result animal_default_ctor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    ydebug("obj=%p", (void *)obj);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yanimal:animal:animal_dtor")]]
static struct yetty_ycore_void_result animal_default_dtor(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    ydebug("obj=%p", (void *)obj);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yanimal:animal:animal_breathe")]]
static struct yetty_ycore_void_result animal_default_breathe(struct ctx *ctx, struct object *obj)
{
    (void)ctx;
    ydebug("obj=%p", (void *)obj);
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yanimal:animal:animal_speak")]]
static struct str_result animal_default_speak(struct ctx *ctx, struct object *obj, int volume)
{
    (void)ctx;
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "animal@%p makes a generic sound (volume=%d)", (void *)obj,
             volume);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

[[clang::annotate("override@yanimal:animal:animal_eat")]]
static struct yetty_ycore_int_result animal_default_eat(struct ctx *ctx, struct object *obj,
                                                        float amount)
{
    (void)ctx;
    ydebug("obj=%p amount=%.1fkg", (void *)obj, amount);
    return YETTY_OK(yetty_ycore_int, 0);
}

#include "animal.gen.c"
