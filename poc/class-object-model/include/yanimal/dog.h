/* GENERATED — do not edit. */
/* Public interface for regular class `dog` (module: yanimal).
 * GENERATED — do not edit. Edit the annotated source under src/yanimal/ instead. */
#ifndef POC_YANIMAL_DOG_H
#define POC_YANIMAL_DOG_H

#include "class.h"
#include "methods.gen.h"  /* every public method stub in this module */
#include "yanimal/animal.h"
#include "yanimal/pet.h"

/* Data-block handle — opaque outside the owning .c. */
struct dog_data;
struct dog_data *yanimal_dog_data(struct object *obj);
/* Member accessors — the public way to reach the data. */
int yanimal_dog_loyalty_get(struct object *obj);
void yanimal_dog_loyalty_set(struct object *obj, int value);

struct class_ptr_result yanimal_dog_class_get(void);

#endif
