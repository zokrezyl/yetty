/* GENERATED — do not edit. */
/* Public interface for mixin `pet` (module: yanimal).
 * GENERATED — do not edit. Edit the annotated source under src/yanimal/ instead. */
#ifndef POC_YANIMAL_PET_H
#define POC_YANIMAL_PET_H

#include "class.h"
#include "methods.gen.h" /* every public method stub in this module */

/* Data-block handle — opaque outside the owning .c. The
 * struct stays private; only its pointer crosses here, in a
 * Result so a bad object surfaces rather than corrupting. */
struct pet_data;
YETTY_YRESULT_DECLARE(yanimal_pet_data_ptr, struct pet_data *);
struct yanimal_pet_data_ptr_result yanimal_pet_data_get(struct object *obj);
/* Member accessors — the public way to reach the data. */
struct yetty_ycore_int_result yanimal_pet_treats_today_get(struct object *obj);
struct yetty_ycore_void_result yanimal_pet_treats_today_set(struct object *obj, int value);

struct class_ptr_result yanimal_pet_mixin_get(void);

#endif
