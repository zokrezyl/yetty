/* GENERATED — do not edit. */
/* Public interface for regular class(es) `container` (module: yfigure).
 * Fully generated from the source .c — do not edit. This single
 * header is the source's complete public interface: class
 * accessors, method stubs, create()/register(), exposed
 * functions, and the public types the signatures use. */
#ifndef YETTY_YCLASSGEN_YFIGURE_CONTAINER_H
#define YETTY_YCLASSGEN_YFIGURE_CONTAINER_H

#include <yetty/yclass/class.h>
#include <yetty/yclass/rpc.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_context;
struct yetty_ycore_rectangle;
struct yetty_yfigure_figure;
struct yetty_yfigure_registry;
struct yetty_ywire_wire_statemachine;

/* Hit-test result: the child whose rect contains the cursor, plus the cursor
 * coordinates inside that child's own pixel space (origin = child rect's
 * top-left). figure_id == 0 means "no hit". Iteration is back-to-front, so
 * for overlapping children the BACK-most match wins. This is an `expose`d
 * type: codegen reads this definition (it sees the whole TU during its parse
 * pass) and re-emits it into the generated container.h for consumers. The
 * definition lives HERE because this TU no longer includes container.h, so it
 * is the sole definition in the real build too — no double definition.
 * `expose` makes codegen re-emit this full definition (it is returned by
 * value from yetty_yfigure_container_hit_test, so consumers need the
 * layout, not just a forward decl) into the generated container.h. That
 * header copy and this one never share a TU — this TU does not include
 * container.h — so there is no clash. */
struct yetty_yfigure_hit {
    uint32_t figure_id;
    float local_x;
    float local_y;
};

struct yetty_yclass_ptr_result yetty_yfigure_container_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yfigure_container;
struct yetty_yfigure_container_ptr_result {
    int ok;
    union {
        struct yetty_yfigure_container *value;
        struct yetty_ycore_error error;
    };
};
struct yetty_yfigure_container_ptr_result yetty_yfigure_container_from(struct yetty_yclass_object *obj);
struct yetty_yclass_object *yetty_yfigure_container_to(struct yetty_yfigure_container *data);

/* yclass instance layout: yclass_object header at offset 0, user data
 * (the `struct yetty_yfigure_container` body) immediately after. Cast
 * via (obj + 1) advances past the header in pointer arithmetic. */
struct yetty_ycore_void_result yetty_yfigure_constructor(struct yetty_yclass_object * obj);
struct yetty_ycore_void_result yetty_yfigure_add_child(struct yetty_yclass_object * obj, struct yetty_yfigure_figure * child, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_remove_child_by_id(struct yetty_yclass_object * obj, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_raise_child_by_id(struct yetty_yclass_object * obj, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_process_records(struct yetty_yclass_object * obj, struct yetty_ycore_buffer bytes);

typedef struct yetty_ycore_void_result (*yetty_yfigure_constructor_fn)(struct yetty_yclass_object *);
typedef struct yetty_ycore_void_result (*yetty_yfigure_add_child_fn)(struct yetty_yclass_object *, struct yetty_yfigure_figure *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_remove_child_by_id_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_raise_child_by_id_fn)(struct yetty_yclass_object *, uint32_t);
typedef struct yetty_ycore_void_result (*yetty_yfigure_process_records_fn)(struct yetty_yclass_object *, struct yetty_ycore_buffer);

struct yetty_yclass_object_ptr_result yetty_yfigure_container_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_void_result yetty_yfigure_register(void);

struct yetty_ycore_char_ptr_result yetty_yfigure_dump(const struct yetty_yfigure_figure *self, int indent);
/* Remove and destroy every child figure, then mark the container dirty so the
 * empty result is repainted. Shared by the CLEAR_ALL admin op and the terminal's
 * full-screen-erase / reset path. Best-effort: keeps tearing down on a per-child
 * error, stashing the first to surface at the end. */
struct yetty_ycore_void_result yetty_yfigure_container_clear_all(struct yetty_yclass_object *obj);
/* Setters for per-instance runtime state. These are owner-side
 * helpers — they take a body pointer (not a proxy), so they're only
 * meaningful on the side that hosts the actual container instance.
 * That side knows its `context` and `registry` from local C state;
 * neither pointer is meaningful across a wire. */
void yetty_yfigure_container_set_registry(struct yetty_yclass_object *obj, struct yetty_yfigure_registry *registry);
void yetty_yfigure_container_set_context(struct yetty_yclass_object *obj, const struct yetty_context *context);
struct yetty_ycore_void_result yetty_yfigure_container_set_rect(struct yetty_yclass_object *obj, struct yetty_ycore_rectangle rect);
void yetty_yfigure_container_set_viewport_offset(struct yetty_yclass_object *obj, float offset_x, float offset_y);
struct yetty_ycore_void_result yetty_yfigure_container_consume_envelope(struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *sm);
/* Coroutine entry registered on the input pipe — its signature is fixed by
 * the wire callback ABI (`yetty_ywire_process_input_fn` = void *userdata, sm),
 * so the handle arrives as an opaque `userdata` that callers set to the
 * container's yclass object. */
struct yetty_ycore_void_result yetty_yfigure_container_process_input(void *userdata, struct yetty_ywire_wire_statemachine *sm);
struct yetty_ycore_void_result yetty_yfigure_container_process_records(struct yetty_yclass_object *obj, const uint8_t *bytes, size_t bytes_len);
struct yetty_yfigure_figure *yetty_yfigure_container_as_figure(struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_container_add_child(struct yetty_yclass_object *obj, struct yetty_yfigure_figure *child, uint32_t id);
struct yetty_yfigure_figure *yetty_yfigure_container_find_child_by_id(struct yetty_yclass_object *obj, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_container_remove_child_by_id(struct yetty_yclass_object *obj, uint32_t id);
/* Mark an existing child as protected from clear_all (the CLEAR_ALL admin op
 * and the terminal's full-screen-erase / reset hook). The container's own
 * structural figures — the terminal content grid above all — use this so a
 * client emitting CLEAR_ALL to drop its figures cannot also wipe the host's
 * content. Idempotent; a stale id (already removed) is a benign no-op. */
struct yetty_ycore_void_result yetty_yfigure_container_protect_child(struct yetty_yclass_object *obj, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_container_raise_child_by_id(struct yetty_yclass_object *obj, uint32_t id);
struct yetty_yfigure_hit yetty_yfigure_container_hit_test(struct yetty_yclass_object *obj, float x, float y);

#ifdef __cplusplus
}
#endif

#endif
