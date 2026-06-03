/* GENERATED — do not edit. */
/* Public interface for regular class `animal` (module: yanimal).
 * GENERATED — do not edit. Edit the annotated source under src/yanimal/ instead. */
#ifndef POC_YANIMAL_ANIMAL_H
#define POC_YANIMAL_ANIMAL_H

#include "class.h"
#include "methods.gen.h"  /* every public method stub in this module */

/* Data-block handle — opaque outside the owning .c. */
struct animal_data;
struct animal_data *yanimal_animal_data(struct object *obj);
/* Member accessors — the public way to reach the data. */
int yanimal_animal_age_get(struct object *obj);
void yanimal_animal_age_set(struct object *obj, int value);
int yanimal_animal_energy_get(struct object *obj);
void yanimal_animal_energy_set(struct object *obj, int value);

struct class_ptr_result yanimal_animal_class_get(void);

#endif
