/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YVIEW_METHODS_GEN_H
#define YETTY_YCLASSGEN_YVIEW_METHODS_GEN_H

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list;

struct yetty_ycore_void_result yetty_yview_configure(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj, int fd,
                                                     uint32_t child_id, uint32_t kind,
                                                     uint32_t bg_color, float min_x, float min_y,
                                                     float max_x, float max_y);
struct yetty_ycore_void_result yetty_yview_set_content(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    const struct yetty_ydraw_drawable_list *content);
struct yetty_ycore_void_result yetty_yview_set_text(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj,
                                                    const char *text, float font_size);
struct yetty_ycore_void_result yetty_yview_set_plot(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj,
                                                    const char *expr, float x_min, float x_max,
                                                    float y_min, float y_max);
struct yetty_ycore_void_result yetty_yview_set_content_size(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj,
                                                            float content_w, float content_h);
struct yetty_ycore_void_result yetty_yview_scroll_to(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj,
                                                     float scroll_x, float scroll_y);
struct yetty_ycore_void_result yetty_yview_scroll_by(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj, float delta_x,
                                                     float delta_y);
struct yetty_ycore_void_result yetty_yview_set_rect(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj, float min_x,
                                                    float min_y, float max_x, float max_y);
struct yetty_ycore_void_result yetty_yview_destroy(struct yetty_yclass_ctx *ctx,
                                                   struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_yview_configure_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *,
                                                                   int, uint32_t, uint32_t,
                                                                   uint32_t, float, float, float,
                                                                   float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_content_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *,
    const struct yetty_ydraw_drawable_list *);
typedef struct yetty_ycore_void_result (*yetty_yview_set_text_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *,
                                                                  const char *, float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_plot_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *,
                                                                  const char *, float, float, float,
                                                                  float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_content_size_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_scroll_to_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *,
                                                                   float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_scroll_by_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *,
                                                                   float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_set_rect_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *,
                                                                  float, float, float, float);
typedef struct yetty_ycore_void_result (*yetty_yview_destroy_fn)(struct yetty_yclass_ctx *,
                                                                 struct yetty_yclass_object *);

#endif
