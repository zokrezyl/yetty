/* GENERATED — do not edit. */
/* Public interface for regular class `cat` (module: yanimal).
 * GENERATED — do not edit. Edit the annotated source under src/yanimal/ instead. */
#ifndef POC_YANIMAL_CAT_H
#define POC_YANIMAL_CAT_H

#include "class.h"
#include "methods.gen.h" /* every public method stub in this module */
#include "yanimal/animal.h"
#include "yanimal/pet.h"

/* Data-block handle — opaque outside the owning .c. The
 * struct stays private; only its pointer crosses here, in a
 * Result so a bad object surfaces rather than corrupting. */
struct cat_data;
YETTY_YRESULT_DECLARE(yanimal_cat_data_ptr, struct cat_data *);
struct yanimal_cat_data_ptr_result yanimal_cat_data_get(struct object *obj);
/* Member accessors — the public way to reach the data. */
struct yetty_ycore_int_result yanimal_cat_lives_remaining_get(struct object *obj);
struct yetty_ycore_void_result yanimal_cat_lives_remaining_set(struct object *obj, int value);

struct class_ptr_result yanimal_cat_class_get(void);

#endif
