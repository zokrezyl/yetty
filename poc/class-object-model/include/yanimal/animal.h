/* GENERATED — do not edit. */
/* Public interface for regular class `animal` (module: yanimal).
 * GENERATED — do not edit. Edit the annotated source under src/yanimal/ instead. */
#ifndef POC_YANIMAL_ANIMAL_H
#define POC_YANIMAL_ANIMAL_H

#include "class.h"
#include "methods.gen.h"  /* every public method stub in this module */

/* Data-block handle — opaque outside the owning .c. The
 * struct stays private; only its pointer crosses here, in a
 * Result so a bad object surfaces rather than corrupting. */
struct animal_data;
YETTY_YRESULT_DECLARE(yanimal_animal_data_ptr, struct animal_data *);
struct yanimal_animal_data_ptr_result yanimal_animal_data_get(struct object *obj);
/* Member accessors — the public way to reach the data. */
struct yetty_ycore_int_result yanimal_animal_age_get(struct object *obj);
struct yetty_ycore_void_result yanimal_animal_age_set(struct object *obj, int value);
struct yetty_ycore_int_result yanimal_animal_energy_get(struct object *obj);
struct yetty_ycore_void_result yanimal_animal_energy_set(struct object *obj, int value);

struct class_ptr_result yanimal_animal_class_get(void);

#endif
