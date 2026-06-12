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

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_yplot;
YETTY_YRESULT_DECLARE(yetty_ygui_yplot_ptr, struct yetty_ygui_yplot *);
struct yetty_ygui_yplot_ptr_result yetty_ygui_yplot_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ygui_yplot_to(struct yetty_ygui_yplot *data);

struct yetty_yclass_object_ptr_result yetty_ygui_yplot_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ygui_yplot_config;
struct yetty_yplot_buffer_input;

#include <stddef.h>
#include <stdint.h>
struct yetty_yclass_object;
struct yetty_ygui_yplot_config {
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    uint32_t flags;
};
struct yetty_yplot_buffer_input;
struct yetty_ycore_void_result yetty_ygui_yplot_set_source(struct yetty_yclass_object *obj, const char *source);
struct yetty_ycore_void_result yetty_ygui_yplot_set_config(struct yetty_yclass_object *obj, const struct yetty_ygui_yplot_config *cfg);
struct yetty_ycore_void_result yetty_ygui_yplot_set_buffers(struct yetty_yclass_object *obj, const char *source, size_t source_len, const struct yetty_yplot_buffer_input *buffers, size_t buffer_count, const struct yetty_ygui_yplot_config *config);

#endif
