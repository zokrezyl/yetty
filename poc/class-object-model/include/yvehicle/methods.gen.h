/* GENERATED — do not edit. */
#ifndef POC_YVEHICLE_METHODS_GEN_H
#define POC_YVEHICLE_METHODS_GEN_H

#include "class.h"


void vehicle_ctor(struct ctx * ctx, struct object * obj);
void vehicle_dtor(struct ctx * ctx, struct object * obj);
void vehicle_start(struct ctx * ctx, struct object * obj);
int vehicle_accelerate(struct ctx * ctx, struct object * obj, float speed);
int vehicle_brake(struct ctx * ctx, struct object * obj, float intensity);
struct str vehicle_describe(struct ctx * ctx, struct object * obj, float distance);

#endif
