/* GENERATED — do not edit. */
#ifndef POC_YANIMAL_METHODS_GEN_H
#define POC_YANIMAL_METHODS_GEN_H

#include "class.h"


void animal_ctor(struct ctx * ctx, struct object * obj);
void animal_dtor(struct ctx * ctx, struct object * obj);
void animal_breathe(struct ctx * ctx, struct object * obj);
struct str animal_speak(struct ctx * ctx, struct object * obj, int volume);
int animal_eat(struct ctx * ctx, struct object * obj, float amount);

#endif
