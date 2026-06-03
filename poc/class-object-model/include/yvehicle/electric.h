/* GENERATED — do not edit. */
/* Public interface for mixin `electric` (module: yvehicle).
 * GENERATED — do not edit. Edit the annotated source under src/yvehicle/ instead. */
#ifndef POC_YVEHICLE_ELECTRIC_H
#define POC_YVEHICLE_ELECTRIC_H

#include "class.h"
#include "methods.gen.h"  /* every public method stub in this module */

/* Data-block handle — opaque outside the owning .c. */
struct electric_data;
struct electric_data *yvehicle_electric_data(struct object *obj);
/* Member accessors — the public way to reach the data. */
int yvehicle_electric_battery_percent_get(struct object *obj);
void yvehicle_electric_battery_percent_set(struct object *obj, int value);

struct class_ptr_result yvehicle_electric_mixin_get(void);

#endif
