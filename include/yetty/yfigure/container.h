/* GENERATED — do not edit. */
/* Public interface for regular class(es) `container` (module: yfigure).
 * Fully generated from the source .c — do not edit. Function
 * and public-type APIs come from `expose` annotations; the
 * forward declarations are derived from the prototype types. */
#ifndef YETTY_YCLASSGEN_YFIGURE_CONTAINER_H
#define YETTY_YCLASSGEN_YFIGURE_CONTAINER_H

#include <yetty/yclass/class.h>
#include <yetty/yfigure/methods.h>

struct yetty_yclass_ptr_result yetty_yfigure_container_class_get(void);

struct yetty_context;
struct yetty_ycore_rectangle;
struct yetty_yfigure_container;
struct yetty_yfigure_figure;
struct yetty_yfigure_registry;
struct yetty_ywire_wire_statemachine;

struct yetty_yfigure_hit {
    uint32_t figure_id;
    float local_x;
    float local_y;
};
struct yetty_ycore_char_ptr_result yetty_yfigure_dump(const struct yetty_yfigure_figure *self,
                                                      int indent);
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
/* Remove and destroy every child figure (CLEAR_ALL admin op; also the terminal's full-screen erase / reset path). */
struct yetty_ycore_void_result yetty_yfigure_container_clear_all(
    struct yetty_yfigure_container *container);
struct yetty_ycore_void_result yetty_yfigure_container_raise_child_by_id(
    struct yetty_yfigure_container *container, uint32_t id);
struct yetty_yfigure_hit yetty_yfigure_container_hit_test(struct yetty_yfigure_container *container,
                                                          float x, float y);

#endif
