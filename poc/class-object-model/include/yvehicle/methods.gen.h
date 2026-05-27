/* GENERATED — do not edit. */
#ifndef POC_YVEHICLE_METHODS_GEN_H
#define POC_YVEHICLE_METHODS_GEN_H

#include "class.h"


void yvehicle_vehicle_ctor(struct ctx * ctx, struct object * obj);
void yvehicle_vehicle_dtor(struct ctx * ctx, struct object * obj);
void yvehicle_vehicle_start(struct ctx * ctx, struct object * obj);
int yvehicle_vehicle_accelerate(struct ctx * ctx, struct object * obj, float speed);
int yvehicle_vehicle_brake(struct ctx * ctx, struct object * obj, float intensity);
struct str yvehicle_vehicle_describe(struct ctx * ctx, struct object * obj, float distance);

#endif
