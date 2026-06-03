/* GENERATED — do not edit. */
/* Public interface for regular class `dog` (module: yanimal).
 * GENERATED — do not edit. Edit the annotated source under src/yanimal/ instead. */
#ifndef POC_YANIMAL_DOG_H
#define POC_YANIMAL_DOG_H

#include "class.h"
#include "methods.gen.h"  /* every public method stub in this module */
#include "yanimal/animal.h"
#include "yanimal/pet.h"

/* Data-block handle — opaque outside the owning .c. The
 * struct stays private; only its pointer crosses here, in a
 * Result so a bad object surfaces rather than corrupting. */
struct dog_data;
YETTY_YRESULT_DECLARE(yanimal_dog_data_ptr, struct dog_data *);
struct yanimal_dog_data_ptr_result yanimal_dog_data_get(struct object *obj);
/* Member accessors — the public way to reach the data. */
struct yetty_ycore_int_result yanimal_dog_loyalty_get(struct object *obj);
struct yetty_ycore_void_result yanimal_dog_loyalty_set(struct object *obj, int value);

struct class_ptr_result yanimal_dog_class_get(void);

#endif
