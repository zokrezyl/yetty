/* GENERATED — do not edit. */
#ifndef YETTY_YCLASSGEN_YFIGURE_METHODS_GEN_H
#define YETTY_YCLASSGEN_YFIGURE_METHODS_GEN_H

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_ycore_char_ptr_result;
struct yetty_ycore_void_result;
struct yetty_ydraw_target;
struct yetty_yfigure_figure;
struct yetty_ywire_wire_statemachine;

struct yetty_ycore_void_result yetty_yfigure_destroy(struct yetty_yclass_ctx *ctx,
                                                     struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_render(struct yetty_yclass_ctx *ctx,
                                                    struct yetty_yclass_object *obj,
                                                    struct yetty_ydraw_target *target);
struct yetty_ycore_void_result yetty_yfigure_constructor(struct yetty_yclass_ctx *ctx,
                                                         struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_add_child(struct yetty_yclass_ctx *ctx,
                                                       struct yetty_yclass_object *obj,
                                                       struct yetty_yfigure_figure *child,
                                                       uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_remove_child_by_id(struct yetty_yclass_ctx *ctx,
                                                                struct yetty_yclass_object *obj,
                                                                uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_raise_child_by_id(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj,
                                                               uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_process_records(struct yetty_yclass_ctx *ctx,
                                                             struct yetty_yclass_object *obj,
                                                             struct yetty_ycore_buffer bytes);
struct yetty_ycore_void_result yetty_yfigure_process_input(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ywire_wire_statemachine *statemachine);
struct yetty_ycore_void_result yetty_yfigure_process_bytes(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj,
                                                           const uint8_t *bytes, size_t bytes_len);
struct yetty_ycore_char_ptr_result yetty_yfigure_dump_state(struct yetty_yclass_ctx *ctx,
                                                            struct yetty_yclass_object *obj,
                                                            int indent);
struct yetty_ycore_void_result yetty_yfigure_reset_content(struct yetty_yclass_ctx *ctx,
                                                           struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_set_scroll(struct yetty_yclass_ctx *ctx,
                                                        struct yetty_yclass_object *obj,
                                                        float scroll_x, float scroll_y);
struct yetty_ycore_void_result yetty_yfigure_set_content_size(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              float content_w, float content_h);

typedef struct yetty_ycore_void_result (*yetty_yfigure_destroy_fn)(struct yetty_yclass_ctx *,
                                                                   struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_render_fn)(struct yetty_yclass_ctx *,
                                                                  struct yetty_yclass_object *,
                                                                  struct yetty_ydraw_target *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_constructor_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_add_child_fn)(struct yetty_yclass_ctx *,
                                                                     struct yetty_yclass_object *,
                                                                     struct yetty_yfigure_figure *,
                                                                     uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_remove_child_by_id_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_raise_child_by_id_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_records_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, struct yetty_ycore_buffer);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_input_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *,
    struct yetty_ywire_wire_statemachine *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_bytes_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, const uint8_t *, size_t);
typedef struct yetty_ycore_char_ptr_result (*yetty_yfigure_dump_state_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, int);
typedef struct yetty_ycore_void_result (*yetty_yfigure_reset_content_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_scroll_fn)(struct yetty_yclass_ctx *,
                                                                      struct yetty_yclass_object *,
                                                                      float, float);
typedef struct yetty_ycore_void_result (*yetty_yfigure_set_content_size_fn)(
    struct yetty_yclass_ctx *, struct yetty_yclass_object *, float, float);

#endif
