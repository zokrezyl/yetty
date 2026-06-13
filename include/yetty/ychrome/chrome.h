/* GENERATED — do not edit. */
/* Public interface for regular class(es) `chrome` (module: ychrome).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YCHROME_CHROME_H
#define YETTY_YCLASSGEN_YCHROME_CHROME_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/yevent/event.h>

struct yetty_ychrome_chrome;

struct yetty_ychrome_chrome_ptr_result {
    int ok;
    union {
        struct yetty_ychrome_chrome *value;
        struct yetty_ycore_error error;
    };
};

enum yetty_ychrome_flag {
    YETTY_YCHROME_FLAG_DRAG = 1,
    YETTY_YCHROME_FLAG_RESIZE = 2,
    YETTY_YCHROME_FLAG_MAXIMIZE = 4,
    YETTY_YCHROME_FLAG_ALL = 7,
};

struct yetty_yclass_ptr_result yetty_ychrome_chrome_class_get(void);

/* Data-block accessors. The data struct stays private (only a
 * forward declaration crosses into the header); reach members
 * through the per-property getters/setters below. */
struct yetty_ychrome_chrome_ptr_result yetty_ychrome_chrome_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_ychrome_chrome_to(struct yetty_ychrome_chrome *data);

struct yetty_ycore_void_result yetty_ychrome_configure(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yetty_yclass_object *window_manager,
                                                       float caption_height, float edge_size,
                                                       uint32_t flags);
struct yetty_ycore_void_result yetty_ychrome_set_size(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj, float width,
                                                      float height);
struct yetty_ycore_void_result yetty_ychrome_destroy(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ychrome_edge_cursor_at(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj, float x,
                                                           float y);
struct yetty_ydraw_drawable_list_result yetty_ychrome_render(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_ychrome_handle_event(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj,
                                                         const struct yetty_yui_event *event);

typedef struct yetty_ycore_void_result (*yetty_ychrome_configure_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *,
                                                                     struct yetty_yclass_object *,
                                                                     float, float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_ychrome_set_size_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    float, float);
typedef struct yetty_ycore_void_result (*yetty_ychrome_destroy_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ychrome_edge_cursor_at_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
typedef struct yetty_ydraw_drawable_list_result (*yetty_ychrome_render_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_ychrome_handle_event_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, const struct yetty_yui_event *);

struct yetty_yclass_object_ptr_result yetty_ychrome_chrome_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_ychrome_register(void);

struct yetty_ycore_int_result yetty_ychrome_hover_button(struct yetty_yclass_object *obj);

#endif
