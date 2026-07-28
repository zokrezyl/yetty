/* GENERATED — do not edit. */
/* Public interface for regular class(es) `yplot` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_YPLOT_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_YPLOT_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yplot_buffer_input;

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_YPLOT_CONFIG
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_YPLOT_CONFIG
/* Plot configuration. Defined here in the owning .c; codegen reproduces it
 * into the generated header (consumers fill it by value, so the full
 * definition is published via the `expose` annotation). */
struct yetty_ygui_yplot_config {
    float x_min;
    float x_max;
    float y_min;
    float y_max;
    uint32_t flags;
};
#endif

struct yetty_yclass_ptr_result yetty_ygui_yplot_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_yplot;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YGUI_YPLOT_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YGUI_YPLOT_PTR_RESULT
struct yetty_ygui_yplot_ptr_result {
    int ok;
    union {
        struct yetty_ygui_yplot *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_ygui_yplot_ptr_result yetty_ygui_yplot_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_yplot_to(struct yetty_ygui_yplot *data);

struct yetty_yclass_object_ptr_result yetty_ygui_yplot_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_yplot_set_source(struct yetty_yclass_object *obj,
                                                           const char *source);
struct yetty_ycore_void_result yetty_ygui_yplot_set_config(
    struct yetty_yclass_object *obj, const struct yetty_ygui_yplot_config *cfg);
struct yetty_ycore_void_result yetty_ygui_yplot_set_buffers(
    struct yetty_yclass_object *obj, const char *source, size_t source_len,
    const struct yetty_yplot_buffer_input *buffers, size_t buffer_count,
    const struct yetty_ygui_yplot_config *config);

#ifdef __cplusplus
}
#endif

#endif
