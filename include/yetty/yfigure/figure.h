/* GENERATED — do not edit. */
/* Public interface for regular class(es) `figure` (module: yfigure).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YFIGURE_FIGURE_H
#define YETTY_YCLASSGEN_YFIGURE_FIGURE_H

#include <yetty/yclass/class.h>
#include <yetty/yfigure/methods.h>

struct yetty_yclass_ptr_result yetty_yfigure_figure_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yfigure_figure;
YETTY_YRESULT_DECLARE(yetty_yfigure_figure_data_ptr, struct yetty_yfigure_figure *);
struct yetty_yfigure_figure_data_ptr_result yetty_yfigure_figure_data_get(struct yetty_yclass_object *obj);
struct rectangle_result yetty_yfigure_figure_rect_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_rect_set(struct yetty_yclass_object *obj, struct yetty_ycore_rectangle value);
struct yetty_ycore_int_result yetty_yfigure_figure_z_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_z_set(struct yetty_yclass_object *obj, int32_t value);
struct yetty_ycore_int_result yetty_yfigure_figure_hidden_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_hidden_set(struct yetty_yclass_object *obj, int value);
struct yetty_ycore_int_result yetty_yfigure_figure_dirty_get(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_figure_dirty_set(struct yetty_yclass_object *obj, int value);

#endif
