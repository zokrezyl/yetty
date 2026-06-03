/* GENERATED — do not edit. */
/* Public interface for regular class `car` (module: yvehicle).
 * GENERATED — do not edit. Edit the annotated source under src/yvehicle/ instead. */
#ifndef POC_YVEHICLE_CAR_H
#define POC_YVEHICLE_CAR_H

#include "class.h"
#include "methods.gen.h"  /* every public method stub in this module */
#include "yvehicle/vehicle.h"

/* Data-block handle — opaque outside the owning .c. */
struct car_data;
struct car_data *yvehicle_car_data(struct object *obj);
/* Member accessors — the public way to reach the data. */
int yvehicle_car_doors_get(struct object *obj);
void yvehicle_car_doors_set(struct object *obj, int value);

struct class_ptr_result yvehicle_car_class_get(void);

#endif
