/* GENERATED — do not edit. */
/* Public interface for regular class `cat` (module: yanimal).
 * GENERATED — do not edit. Edit the annotated source under src/yanimal/ instead. */
#ifndef POC_YANIMAL_CAT_H
#define POC_YANIMAL_CAT_H

#include "class.h"
#include "methods.gen.h"  /* every public method stub in this module */
#include "yanimal/animal.h"
#include "yanimal/pet.h"

/* Data-block handle — opaque outside the owning .c. */
struct cat_data;
struct cat_data *yanimal_cat_data(struct object *obj);
/* Member accessors — the public way to reach the data. */
int yanimal_cat_lives_remaining_get(struct object *obj);
void yanimal_cat_lives_remaining_set(struct object *obj, int value);

struct class_ptr_result yanimal_cat_class_get(void);

#endif
