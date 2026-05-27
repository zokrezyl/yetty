/* Cat — animal subclass, uses pet mixin. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include <stdio.h>

struct [[clang::annotate("class@yanimal:cat")]]
       [[clang::annotate("parent@yanimal:animal")]]
       [[clang::annotate("uses@yanimal:pet")]] cat_data {
    int lives_remaining;
};

[[clang::annotate("override@yanimal:cat:animal_speak")]]
static struct str_result cat_speak(struct ctx *ctx, struct object *obj, int volume)
{
    (void)ctx;
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "cat@%p: meow! (volume=%d)", (void *)obj, volume);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "cat.gen.c"
