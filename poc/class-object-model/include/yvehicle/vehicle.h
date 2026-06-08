/* GENERATED — do not edit. */
/* Public interface for regular class `vehicle` (module: yvehicle).
 * GENERATED — do not edit. Edit the annotated source under src/yvehicle/ instead. */
#ifndef POC_YVEHICLE_VEHICLE_H
#define POC_YVEHICLE_VEHICLE_H

#include "class.h"
#include "methods.gen.h" /* every public method stub in this module */

/* Data-block handle — opaque outside the owning .c. The
 * struct stays private; only its pointer crosses here, in a
 * Result so a bad object surfaces rather than corrupting. */
struct vehicle_data;
YETTY_YRESULT_DECLARE(yvehicle_vehicle_data_ptr, struct vehicle_data *);
struct yvehicle_vehicle_data_ptr_result yvehicle_vehicle_data_get(struct object *obj);
/* Member accessors — the public way to reach the data. */
struct yetty_ycore_int_result yvehicle_vehicle_mileage_get(struct object *obj);
struct yetty_ycore_void_result yvehicle_vehicle_mileage_set(struct object *obj, int value);
struct yetty_ycore_int_result yvehicle_vehicle_speed_get(struct object *obj);
struct yetty_ycore_void_result yvehicle_vehicle_speed_set(struct object *obj, int value);

struct class_ptr_result yvehicle_vehicle_class_get(void);

#endif
