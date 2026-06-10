/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YCHROME_METHODS_GEN_H
#define YETTY_YCLASSGEN_YCHROME_METHODS_GEN_H

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;
struct yetty_yui_event;

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

#endif
