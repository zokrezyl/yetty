/* GENERATED — do not edit. */
#ifndef POC_YVEHICLE_METHODS_GEN_H
#define POC_YVEHICLE_METHODS_GEN_H

#include "class.h"

struct str_result;
struct yetty_ycore_int_result;
struct yetty_ycore_void_result;

struct yetty_ycore_void_result yvehicle_vehicle_ctor(struct object * obj);
typedef struct yetty_ycore_void_result (*yvehicle_vehicle_ctor_fn)(struct object *);
struct yetty_ycore_void_result yvehicle_vehicle_dtor(struct object * obj);
typedef struct yetty_ycore_void_result (*yvehicle_vehicle_dtor_fn)(struct object *);
struct yetty_ycore_void_result yvehicle_vehicle_start(struct object * obj);
typedef struct yetty_ycore_void_result (*yvehicle_vehicle_start_fn)(struct object *);
struct yetty_ycore_int_result yvehicle_vehicle_accelerate(struct object * obj, float speed);
typedef struct yetty_ycore_int_result (*yvehicle_vehicle_accelerate_fn)(struct object *, float);
struct yetty_ycore_int_result yvehicle_vehicle_brake(struct object * obj, float intensity);
typedef struct yetty_ycore_int_result (*yvehicle_vehicle_brake_fn)(struct object *, float);
struct str_result yvehicle_vehicle_describe(struct object * obj, float distance);
typedef struct str_result (*yvehicle_vehicle_describe_fn)(struct object *, float);

#endif
