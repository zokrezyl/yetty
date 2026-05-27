/* GENERATED — do not edit. */
#ifndef POC_YVEHICLE_METHODS_GEN_H
#define POC_YVEHICLE_METHODS_GEN_H

#include "class.h"


void yvehicle_vehicle_ctor(struct ctx * ctx, struct object * obj);
typedef void (*yvehicle_vehicle_ctor_fn)(struct ctx *, struct object *);
void yvehicle_vehicle_dtor(struct ctx * ctx, struct object * obj);
typedef void (*yvehicle_vehicle_dtor_fn)(struct ctx *, struct object *);
void yvehicle_vehicle_start(struct ctx * ctx, struct object * obj);
typedef void (*yvehicle_vehicle_start_fn)(struct ctx *, struct object *);
int yvehicle_vehicle_accelerate(struct ctx * ctx, struct object * obj, float speed);
typedef int (*yvehicle_vehicle_accelerate_fn)(struct ctx *, struct object *, float);
int yvehicle_vehicle_brake(struct ctx * ctx, struct object * obj, float intensity);
typedef int (*yvehicle_vehicle_brake_fn)(struct ctx *, struct object *, float);
struct str yvehicle_vehicle_describe(struct ctx * ctx, struct object * obj, float distance);
typedef struct str (*yvehicle_vehicle_describe_fn)(struct ctx *, struct object *, float);

#endif
