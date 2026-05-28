/* GENERATED — do not edit. */
/* Public interface for regular class(es) `yplot` (module: ygui).
 * Codegen regenerates the section above the MANUAL markers;
 * hand-written content between the markers is preserved
 * across runs. Edit annotated source for accessor + slot
 * changes; edit between MANUAL markers for app-facing
 * helper declarations, enums, etc. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YPLOT_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YPLOT_H

#include <yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_yplot_class_get(void);

/* === MANUAL CONTENT BELOW — preserved across codegen runs === */
/* Replace the expression source. The widget rebuilds its primitive on
 * the next emit. */
struct yetty_ycore_void_result yetty_ygui_yplot_set_source(struct yetty_ygui_object *obj,
                                                           const char *source);

/* Plot configuration — bounds_w / bounds_h are overridden by the
 * resolved layout rect at emit time; the other fields (x_min/max,
 * y_min/max, flags) are honoured. Pass NULL for defaults. */
struct yetty_ygui_yplot_config {
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    uint32_t flags;
};
struct yetty_ycore_void_result yetty_ygui_yplot_set_config(struct yetty_ygui_object *obj,
                                                           const struct yetty_ygui_yplot_config *cfg);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
