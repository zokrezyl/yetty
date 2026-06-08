/* GENERATED — do not edit. */
/* Public interface for regular class `car` (module: yvehicle).
 * GENERATED — do not edit. Edit the annotated source under src/yvehicle/ instead. */
#ifndef POC_YVEHICLE_CAR_H
#define POC_YVEHICLE_CAR_H

#include "class.h"
#include "methods.gen.h" /* every public method stub in this module */
#include "yvehicle/vehicle.h"

/* Data-block handle — opaque outside the owning .c. The
 * struct stays private; only its pointer crosses here, in a
 * Result so a bad object surfaces rather than corrupting. */
struct car_data;
YETTY_YRESULT_DECLARE(yvehicle_car_data_ptr, struct car_data *);
struct yvehicle_car_data_ptr_result yvehicle_car_data_get(struct object *obj);
/* Member accessors — the public way to reach the data. */
struct yetty_ycore_int_result yvehicle_car_doors_get(struct object *obj);
struct yetty_ycore_void_result yvehicle_car_doors_set(struct object *obj, int value);

struct class_ptr_result yvehicle_car_class_get(void);

#endif
