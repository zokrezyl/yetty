/* GENERATED — do not edit. */
/* Public interface for regular class(es) `yplot` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YPLOT_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YPLOT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_ygui_yplot_class_get(void);

struct yetty_ygui_object;
struct yplot_data;
YETTY_YRESULT_DECLARE(yetty_ygui_yplot_data_ptr, struct yplot_data *);
struct yetty_ygui_yplot_data_ptr_result yetty_ygui_yplot_data(struct yetty_ygui_object *obj);

struct yetty_yclass_object_ptr_result yetty_ygui_yplot_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_object;
struct yetty_ygui_yplot_config;
struct yetty_yplot_buffer_input;

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
struct yetty_ycore_void_result yetty_ygui_yplot_set_source(struct yetty_ygui_object *obj,
                                                           const char *source);
struct yetty_ycore_void_result yetty_ygui_yplot_set_config(
    struct yetty_ygui_object *obj, const struct yetty_ygui_yplot_config *cfg);
struct yetty_ycore_void_result yetty_ygui_yplot_set_buffers(
    struct yetty_ygui_object *obj, const char *source, size_t source_len,
    const struct yetty_yplot_buffer_input *buffers, size_t buffer_count,
    const struct yetty_ygui_yplot_config *config);

#endif
