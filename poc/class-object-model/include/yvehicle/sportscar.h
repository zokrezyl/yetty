/* GENERATED — do not edit. */
/* Public interface for regular class `sportscar` (module: yvehicle).
 * GENERATED — do not edit. Edit the annotated source under src/yvehicle/ instead. */
#ifndef POC_YVEHICLE_SPORTSCAR_H
#define POC_YVEHICLE_SPORTSCAR_H

#include "class.h"
#include "methods.gen.h"  /* every public method stub in this module */
#include "yvehicle/car.h"
#include "yvehicle/electric.h"

/* Data-block handle — opaque outside the owning .c. */
struct sportscar_data;
struct sportscar_data *yvehicle_sportscar_data(struct object *obj);
/* Member accessors — the public way to reach the data. */
int yvehicle_sportscar_top_speed_get(struct object *obj);
int yvehicle_sportscar_turbo_engaged_get(struct object *obj);
void yvehicle_sportscar_turbo_engaged_set(struct object *obj, int value);

struct class_ptr_result yvehicle_sportscar_class_get(void);

#endif
