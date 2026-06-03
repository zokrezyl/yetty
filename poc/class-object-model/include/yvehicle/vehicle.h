/* GENERATED — do not edit. */
/* Public interface for regular class `vehicle` (module: yvehicle).
 * GENERATED — do not edit. Edit the annotated source under src/yvehicle/ instead. */
#ifndef POC_YVEHICLE_VEHICLE_H
#define POC_YVEHICLE_VEHICLE_H

#include "class.h"
#include "methods.gen.h"  /* every public method stub in this module */

/* Data-block handle — opaque outside the owning .c. */
struct vehicle_data;
struct vehicle_data *yvehicle_vehicle_data(struct object *obj);
/* Member accessors — the public way to reach the data. */
int yvehicle_vehicle_mileage_get(struct object *obj);
void yvehicle_vehicle_mileage_set(struct object *obj, int value);
int yvehicle_vehicle_speed_get(struct object *obj);
void yvehicle_vehicle_speed_set(struct object *obj, int value);

struct class_ptr_result yvehicle_vehicle_class_get(void);

#endif
