/* Dog — animal subclass, uses pet mixin. */

#include "class.h"
#include "result.h"
#include "ytrace.h"

#include <stdio.h>

struct [[clang::annotate("class@yanimal:dog")]]
       [[clang::annotate("parent@yanimal:animal")]]
       [[clang::annotate("uses@yanimal:pet")]] dog_data {
    int loyalty;
};

[[clang::annotate("override@yanimal:dog:animal_speak")]]
static struct str_result dog_speak(struct ctx *ctx, struct object *obj, int volume)
{
    (void)ctx;
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "dog@%p: woof! (volume=%d)", (void *)obj, volume);
    ydebug("-> '%s'", r.buf);
    return YETTY_OK(str, r);
}

#include "dog.gen.c"
