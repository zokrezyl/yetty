/* GENERATED — do not edit. */
/* Public interface for mixin `pet` (module: yanimal).
 * GENERATED — do not edit. Edit the annotated source under src/yanimal/ instead. */
#ifndef POC_YANIMAL_PET_H
#define POC_YANIMAL_PET_H

#include "class.h"
#include "methods.gen.h"  /* every public method stub in this module */

/* Data-block handle — opaque outside the owning .c. */
struct pet_data;
struct pet_data *yanimal_pet_data(struct object *obj);
/* Member accessors — the public way to reach the data. */
int yanimal_pet_treats_today_get(struct object *obj);
void yanimal_pet_treats_today_set(struct object *obj, int value);

struct class_ptr_result yanimal_pet_mixin_get(void);

#endif
