/* Cat — animal subclass, uses pet mixin. */

#include "class.h"

#include <stdio.h>

struct [[clang::annotate("class@yanimal:cat")]]
       [[clang::annotate("parent@yanimal:animal")]]
       [[clang::annotate("uses@yanimal:pet")]] cat_data {
    int lives_remaining;
};

[[clang::annotate("override@yanimal:cat:animal_speak")]]
static struct str cat_speak(struct ctx *ctx, struct object *obj, int volume)
{
    (void)ctx;
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "cat@%p: meow! (volume=%d)",
             (void *)obj, volume);
    fprintf(stderr, "  [impl] cat_speak -> '%s'\n", r.buf);
    return r;
}

#include "cat.gen.c"
