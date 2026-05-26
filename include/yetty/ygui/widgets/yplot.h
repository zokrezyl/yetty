/*
 * ygui-yplot.h — figure-shaped widget wrapping the YPLOT figure kind.
 *
 * On the wire the widget IS its own figure: emit_container sends
 * CREATE_CHILD(kind=YPLOT) with the widget's rect; emit_body ships a
 * single yplot complex primitive built from the widget's source string
 * (and optional precomputed data buffers).
 *
 * Source syntax is the multi-function plot DSL parsed by yexpr_parse_plot:
 *
 *     "f = sin(x); g = cos(x); @f.color = #FF6B6B"
 */
#ifndef YETTY_YGUI_WIDGETS_YPLOT_H
#define YETTY_YGUI_WIDGETS_YPLOT_H

#include <stddef.h>
#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ygui/class.h>
#include <yetty/ygui/object.h>

#ifdef __cplusplus
extern "C" {
#endif

const struct yetty_ygui_class *yetty_ygui_yplot_class_get(void);

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

#ifdef __cplusplus
}
#endif

#endif
