/* GENERATED — do not edit. */
/* Public interface for regular class(es) `splitter` (module: ygui).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YGUI_WIDGETS_SPLITTER_H
#define YETTY_YCLASSGEN_YGUI_WIDGETS_SPLITTER_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*yetty_ygui_splitter_change_cb)(struct yetty_yclass_object *, float, void *);

struct yetty_yclass_ptr_result yetty_ygui_splitter_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_ygui_splitter;
struct yetty_ygui_splitter_ptr_result {
    int ok;
    union {
        struct yetty_ygui_splitter *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_ygui_splitter_ptr_result yetty_ygui_splitter_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_ygui_splitter_to(struct yetty_ygui_splitter *data);

struct yetty_yclass_object_ptr_result yetty_ygui_splitter_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ygui_register(void);

struct yetty_ycore_void_result yetty_ygui_splitter_set_axis(struct yetty_yclass_object *obj,
                                                            int row);
struct yetty_ycore_int_result yetty_ygui_splitter_get_axis(const struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_ygui_splitter_set_min(struct yetty_yclass_object *obj,
                                                           float min_size);
struct yetty_ycore_void_result yetty_ygui_splitter_on_change(struct yetty_yclass_object *obj,
                                                             yetty_ygui_splitter_change_cb cb,
                                                             void *userdata);

#ifdef __cplusplus
}
#endif

#endif
