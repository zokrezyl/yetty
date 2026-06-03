/* GENERATED — do not edit. */
/* Public interface for regular class `motorbike` (module: yvehicle).
 * GENERATED — do not edit. Edit the annotated source under src/yvehicle/ instead. */
#ifndef POC_YVEHICLE_MOTORBIKE_H
#define POC_YVEHICLE_MOTORBIKE_H

#include "class.h"
#include "methods.gen.h"  /* every public method stub in this module */
#include "yvehicle/vehicle.h"

/* Data-block handle — opaque outside the owning .c. */
struct motorbike_data;
struct motorbike_data *yvehicle_motorbike_data(struct object *obj);
/* Member accessors — the public way to reach the data. */
int yvehicle_motorbike_has_sidecar_get(struct object *obj);
void yvehicle_motorbike_has_sidecar_set(struct object *obj, int value);

struct class_ptr_result yvehicle_motorbike_class_get(void);

#endif
