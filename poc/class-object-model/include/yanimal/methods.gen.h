/* GENERATED — do not edit. */
#ifndef POC_YANIMAL_METHODS_GEN_H
#define POC_YANIMAL_METHODS_GEN_H

#include "class.h"


void yanimal_animal_ctor(struct ctx * ctx, struct object * obj);
void yanimal_animal_dtor(struct ctx * ctx, struct object * obj);
void yanimal_animal_breathe(struct ctx * ctx, struct object * obj);
struct str yanimal_animal_speak(struct ctx * ctx, struct object * obj, int volume);
int yanimal_animal_eat(struct ctx * ctx, struct object * obj, float amount);

#endif
