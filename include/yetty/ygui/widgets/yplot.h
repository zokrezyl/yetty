/* GENERATED — do not edit. */
/* Public interface for regular class(es) `yplot` (module: ygui).
 * Fully generated from the source .c — do not edit. Function
 * APIs come from `expose` annotations; types and other header
 * content from the source's `#ifdef YCLASS_CODEGEN` blocks. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YPLOT_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YPLOT_H

#include <yetty/yclass/class.h>
#include <yetty/ygui/methods.h>

struct yetty_yclass_ptr_result yetty_ygui_yplot_class_get(void);

struct yetty_ygui_object;
struct yplot_data;
YETTY_YRESULT_DECLARE(yetty_ygui_yplot_data_ptr, struct yplot_data *);
struct yetty_ygui_yplot_data_ptr_result yetty_ygui_yplot_data(struct yetty_ygui_object *obj);

#include <stddef.h>
#include <stdint.h>
struct yetty_ygui_object;
struct yetty_ygui_yplot_config {
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    uint32_t flags;
};
struct yetty_yplot_buffer_input;
struct yetty_ycore_void_result yetty_ygui_yplot_set_source(struct yetty_ygui_object *obj, const char *source);
struct yetty_ycore_void_result yetty_ygui_yplot_set_config(struct yetty_ygui_object *obj, const struct yetty_ygui_yplot_config *cfg);
struct yetty_ycore_void_result yetty_ygui_yplot_set_buffers(struct yetty_ygui_object *obj, const char *source, size_t source_len, const struct yetty_yplot_buffer_input *buffers, size_t buffer_count, const struct yetty_ygui_yplot_config *config);

#endif
