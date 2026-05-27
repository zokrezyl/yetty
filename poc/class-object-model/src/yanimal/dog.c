/* Dog — animal subclass, uses pet mixin. */

#include "class.h"

#include <stdio.h>

struct [[clang::annotate("class:dog")]]
       [[clang::annotate("parent:animal")]]
       [[clang::annotate("uses:pet")]] dog_data {
    int loyalty;
};

[[clang::annotate("override:dog:animal_speak")]]
static struct str dog_speak(struct ctx *ctx, struct object *obj, int volume)
{
    (void)ctx;
    struct str r;
    snprintf(r.buf, sizeof(r.buf), "dog@%p: woof! (volume=%d)",
             (void *)obj, volume);
    fprintf(stderr, "  [impl] dog_speak -> '%s'\n", r.buf);
    return r;
}

#include "dog.gen.c"
