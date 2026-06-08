/* GENERATED — do not edit. */
/* Public interface for mixin `electric` (module: yvehicle).
 * GENERATED — do not edit. Edit the annotated source under src/yvehicle/ instead. */
#ifndef POC_YVEHICLE_ELECTRIC_H
#define POC_YVEHICLE_ELECTRIC_H

#include "class.h"
#include "methods.gen.h" /* every public method stub in this module */

/* Data-block handle — opaque outside the owning .c. The
 * struct stays private; only its pointer crosses here, in a
 * Result so a bad object surfaces rather than corrupting. */
struct electric_data;
YETTY_YRESULT_DECLARE(yvehicle_electric_data_ptr, struct electric_data *);
struct yvehicle_electric_data_ptr_result yvehicle_electric_data_get(struct object *obj);
/* Member accessors — the public way to reach the data. */
struct yetty_ycore_int_result yvehicle_electric_battery_percent_get(struct object *obj);
struct yetty_ycore_void_result yvehicle_electric_battery_percent_set(struct object *obj, int value);

struct class_ptr_result yvehicle_electric_mixin_get(void);

#endif
