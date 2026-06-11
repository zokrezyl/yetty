/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YFLAME_METHODS_GEN_H
#define YETTY_YCLASSGEN_YFLAME_METHODS_GEN_H

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_ycore_int_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_drawable_list_result;

struct yetty_ycore_void_result yetty_yflame_configure(struct yetty_yclass_ctx *ctx,
                                                      struct yetty_yclass_object *obj, float width,
                                                      float frame_height, float min_width,
                                                      uint32_t flags);
struct yetty_ycore_void_result yetty_yflame_parse(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj,
                                                  const char *input, size_t len);
struct yetty_ydraw_drawable_list_result yetty_yflame_render(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj);
struct yetty_ycore_int_result yetty_yflame_hit_test(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj, float x,
                                                    float y);
struct yetty_ycore_void_result yetty_yflame_focus(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj, int32_t node_id);
struct yetty_ycore_void_result yetty_yflame_focus_parent(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yflame_reset(struct yetty_yclass_ctx *ctx,
                                                  struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yflame_set_highlight(struct yetty_yclass_ctx *ctx,
                                                          struct yetty_yclass_object *obj,
                                                          int32_t node_id);
struct yetty_ycore_void_result yetty_yflame_destroy(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj);

typedef struct yetty_ycore_void_result (*yetty_yflame_configure_fn)(struct yetty_yclass_ctx *,
                                                                    struct yetty_yclass_object *,
                                                                    float, float, float, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_parse_fn)(struct yetty_yclass_ctx *,
                                                                struct yetty_yclass_object *,
                                                                const char *, size_t);
typedef struct yetty_ydraw_drawable_list_result (*yetty_yflame_render_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_int_result (*yetty_yflame_hit_test_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *,
                                                                  float, float);
typedef struct yetty_ycore_void_result (*yetty_yflame_focus_fn)(struct yetty_yclass_ctx *,
                                                                struct yetty_yclass_object *,
                                                                int32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_focus_parent_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yflame_reset_fn)(struct yetty_yclass_ctx *,
                                                                struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yflame_set_highlight_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int32_t);
typedef struct yetty_ycore_void_result (*yetty_yflame_destroy_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *);

#endif
