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
#include <stddef.h>
#include <stdint.h>
struct yetty_ygui_object;

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
struct yetty_ycore_void_result yetty_ygui_yplot_set_config(
    struct yetty_ygui_object *obj, const struct yetty_ygui_yplot_config *cfg);

/* Raw-data plotting. Replaces the expression source (pass NULL to clear
 * it for a buffer-only plot) and the data buffers (deep-copied — the
 * caller keeps ownership of its arrays). Each buffer renders as a line
 * plot over x_min..x_max with its own colour (0 → palette default).
 * `config` may be NULL to leave the current config untouched. */
struct yetty_yplot_buffer_input;
struct yetty_ycore_void_result yetty_ygui_yplot_set_buffers(
    struct yetty_ygui_object *obj, const char *source, size_t source_len,
    const struct yetty_yplot_buffer_input *buffers, size_t buffer_count,
    const struct yetty_ygui_yplot_config *config);
/* === MANUAL CONTENT ABOVE — preserved across codegen runs === */

#endif
