/* GENERATED — do not edit. */
/* Public interface for regular class `tuned_sportscar` (module: ytuning).
 * GENERATED — do not edit. Edit the annotated source under src/ytuning/ instead. */
#ifndef POC_YTUNING_TUNED_SPORTSCAR_H
#define POC_YTUNING_TUNED_SPORTSCAR_H

#include "class.h"
#include "methods.gen.h"  /* every public method stub in this module */
#include "yvehicle/sportscar.h"

/* Data-block handle — opaque outside the owning .c. */
struct tuned_sportscar_data;
struct tuned_sportscar_data *ytuning_tuned_sportscar_data(struct object *obj);
/* Member accessors — the public way to reach the data. */
int ytuning_tuned_sportscar_boost_level_get(struct object *obj);
void ytuning_tuned_sportscar_boost_level_set(struct object *obj, int value);

struct class_ptr_result ytuning_tuned_sportscar_class_get(void);

#endif
