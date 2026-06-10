/* GENERATED — do not edit. */
/* Public interface for regular class(es) `container` (module: yfigure).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), and any
 * `expose`d API. Public types come from `expose` annotations. */
#ifndef YETTY_YCLASSGEN_YFIGURE_CONTAINER_H
#define YETTY_YCLASSGEN_YFIGURE_CONTAINER_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

struct yetty_yclass_ptr_result yetty_yfigure_container_class_get(void);

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

struct yetty_yclass_object_ptr_result yetty_yfigure_container_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yfigure_register(void);

struct yetty_context;
struct yetty_ycore_rectangle;
struct yetty_yfigure_container;
struct yetty_yfigure_figure;
struct yetty_yfigure_hit;
struct yetty_yfigure_registry;
struct yetty_ywire_wire_statemachine;

struct yetty_yfigure_hit {
    uint32_t figure_id;
    float local_x;
    float local_y;
};
struct yetty_ycore_char_ptr_result yetty_yfigure_dump(const struct yetty_yfigure_figure *self,
                                                      int indent);
struct yetty_ycore_void_result yetty_yfigure_container_clear_all(
    struct yetty_yfigure_container *container);
struct yetty_yfigure_container *yetty_yfigure_container_from(struct yetty_yclass_object *obj);
void yetty_yfigure_container_set_registry(struct yetty_yfigure_container *container,
                                          struct yetty_yfigure_registry *registry);
void yetty_yfigure_container_set_context(struct yetty_yfigure_container *container,
                                         const struct yetty_context *context);
struct yetty_ycore_void_result yetty_yfigure_container_set_rect(
    struct yetty_yfigure_container *container, struct yetty_ycore_rectangle rect);
void yetty_yfigure_container_set_viewport_offset(struct yetty_yfigure_container *container,
                                                 float offset_x, float offset_y);
struct yetty_ycore_void_result yetty_yfigure_container_consume_envelope(
    struct yetty_yfigure_container *container, struct yetty_ywire_wire_statemachine *sm);
struct yetty_ycore_void_result yetty_yfigure_container_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm);
struct yetty_ycore_void_result yetty_yfigure_container_process_records(
    struct yetty_yfigure_container *container, const uint8_t *bytes, size_t bytes_len);
struct yetty_yfigure_figure *yetty_yfigure_container_as_figure(
    struct yetty_yfigure_container *container);
struct yetty_ycore_void_result yetty_yfigure_container_add_child(
    struct yetty_yfigure_container *container, struct yetty_yfigure_figure *child, uint32_t id);
struct yetty_yfigure_figure *yetty_yfigure_container_find_child_by_id(
    const struct yetty_yfigure_container *container, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_container_remove_child_by_id(
    struct yetty_yfigure_container *container, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_container_protect_child(
    struct yetty_yfigure_container *container, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_container_raise_child_by_id(
    struct yetty_yfigure_container *container, uint32_t id);
struct yetty_yfigure_hit yetty_yfigure_container_hit_test(struct yetty_yfigure_container *container,
                                                          float x, float y);

#endif
