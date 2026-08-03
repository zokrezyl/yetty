/* GENERATED — do not edit. */
/* Object API for regular class(es) `container` (implementation module: yfigure).
 * Fully generated from the source .c — do not edit. The API does
 * not encode whether an implementation dispatches in-process or
 * over RPC; it declares the typed methods, create(), properties,
 * exposed functions, and the types those signatures use. */
#ifndef YETTY_YCLASSGEN_API_YFIGURE_CONTAINER_H
#define YETTY_YCLASSGEN_API_YFIGURE_CONTAINER_H

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

#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YFIGURE_HIT
#define YETTY_YCLASSGEN_TYPE_YETTY_YFIGURE_HIT
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
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YFIGURE_HEADER
#define YETTY_YCLASSGEN_TYPE_YETTY_YFIGURE_HEADER
/* Wire record header — a child body buffer is a sequence of
 * (length, id, payload[length]) records. Length-first so a pipeline
 * linter / proxy can walk records without knowing figure semantics.
 * Part of the object-API data contract (formerly <yetty/yfigure/wire.h>). */
struct yetty_yfigure_header {
    uint32_t length;
    uint32_t id;
};
#endif
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YFIGURE_HIT_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YFIGURE_HIT_RESULT
struct yetty_yfigure_hit_result {
    int ok;
    union {
        struct yetty_yfigure_hit value;
        struct yetty_ycore_error error;
    };
};
#endif

struct yetty_yclass_ptr_result yetty_yfigure_container_class_get(void);

/* Data-block handle — opaque outside the owning .c. The struct
 * stays private; only its pointer crosses here, in a Result so a
 * bad object surfaces rather than corrupting. Reach members
 * through the per-property getters/setters below. */
struct yetty_yfigure_container;
#ifndef YETTY_YCLASSGEN_TYPE_YETTY_YFIGURE_CONTAINER_PTR_RESULT
#define YETTY_YCLASSGEN_TYPE_YETTY_YFIGURE_CONTAINER_PTR_RESULT
struct yetty_yfigure_container_ptr_result {
    int ok;
    union {
        struct yetty_yfigure_container *value;
        struct yetty_ycore_error error;
    };
};
#endif
struct yetty_yfigure_container_ptr_result yetty_yfigure_container_from(
    struct yetty_yclass_object *obj);
struct yetty_yclass_object_ptr_result yetty_yfigure_container_to(
    struct yetty_yfigure_container *data);

struct yetty_ycore_void_result yetty_yfigure_add_child(struct yetty_yclass_object *obj,
                                                       struct yetty_yfigure_figure *child,
                                                       uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_remove_child_by_id(struct yetty_yclass_object *obj,
                                                                uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_raise_child_by_id(struct yetty_yclass_object *obj,
                                                               uint32_t id);
/* Every mutation slot is plain request/response — a remote failure comes
 * back to the producer as the stub's Result (full cause chain), never
 * swallowed. The legacy `oneway@` fire-and-forget here hid every server-side
 * failure (bad payload, unknown child id, factory rejection) in the host's
 * log while the producer streamed on against state it believed existed —
 * the direct source of silently missing/stale figures. Round-trip latency
 * on the local transports is microseconds; if profiling ever shows the
 * lockstep wait hurting a remote producer, the answer is pipelined async
 * calls with error completion on the multiplexed connection — not
 * discarding the error channel. */
struct yetty_ycore_void_result yetty_yfigure_create_child(struct yetty_yclass_object *obj,
                                                          uint32_t kind_token, uint32_t id,
                                                          struct yetty_ycore_rectangle rect,
                                                          struct yetty_ycore_buffer init);
struct yetty_ycore_void_result yetty_yfigure_delete_child(struct yetty_yclass_object *obj,
                                                          uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_set_child_rect(struct yetty_yclass_object *obj,
                                                            uint32_t id,
                                                            struct yetty_ycore_rectangle rect);
struct yetty_ycore_void_result yetty_yfigure_set_rect(struct yetty_yclass_object *obj,
                                                      struct yetty_ycore_rectangle rect);
struct yetty_ycore_void_result yetty_yfigure_set_child_z(struct yetty_yclass_object *obj,
                                                         uint32_t id, int32_t z);
struct yetty_ycore_void_result yetty_yfigure_set_child_hidden(struct yetty_yclass_object *obj,
                                                              uint32_t id, uint32_t hidden);
struct yetty_ycore_void_result yetty_yfigure_set_child_scroll(struct yetty_yclass_object *obj,
                                                              uint32_t id, float scroll_x,
                                                              float scroll_y);
struct yetty_ycore_void_result yetty_yfigure_set_child_content_size(struct yetty_yclass_object *obj,
                                                                    uint32_t id, float content_w,
                                                                    float content_h);
struct yetty_ycore_void_result yetty_yfigure_apply_child_body(struct yetty_yclass_object *obj,
                                                              uint32_t id,
                                                              struct yetty_ycore_buffer body);
struct yetty_ycore_void_result yetty_yfigure_clear_all(struct yetty_yclass_object *obj);

struct yetty_yclass_object_ptr_result yetty_yfigure_container_create(struct yetty_yclass_ctx *ctx);

struct yetty_ycore_char_ptr_result yetty_yfigure_dump(const struct yetty_yfigure_figure *self,
                                                      int indent);
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
struct yetty_ycore_void_result yetty_yfigure_container_set_registry(
    struct yetty_yclass_object *obj, struct yetty_yfigure_registry *registry);
struct yetty_ycore_void_result yetty_yfigure_container_set_context(
    struct yetty_yclass_object *obj, const struct yetty_context *context);
struct yetty_ycore_void_result yetty_yfigure_container_set_rect(struct yetty_yclass_object *obj,
                                                                struct yetty_ycore_rectangle rect);
struct yetty_ycore_void_result yetty_yfigure_container_set_viewport_offset(
    struct yetty_yclass_object *obj, float offset_x, float offset_y);
struct yetty_ycore_void_result yetty_yfigure_container_set_scroll_context(
    struct yetty_yclass_object *obj, uint64_t content_root_row, float cell_height);
struct yetty_yfigure_figure_ptr_result yetty_yfigure_container_as_figure(
    struct yetty_yclass_object *obj);
struct yetty_ycore_void_result yetty_yfigure_container_add_child(struct yetty_yclass_object *obj,
                                                                 struct yetty_yfigure_figure *child,
                                                                 uint32_t id);
struct yetty_yfigure_figure_ptr_result yetty_yfigure_container_find_child_by_id(
    struct yetty_yclass_object *obj, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_container_remove_child_by_id(
    struct yetty_yclass_object *obj, uint32_t id);
/* Mark an existing child as protected from clear_all (the CLEAR_ALL admin op
 * and the terminal's full-screen-erase / reset hook). The container's own
 * structural figures — the terminal content grid above all — use this so a
 * client emitting CLEAR_ALL to drop its figures cannot also wipe the host's
 * content. Idempotent; a stale id (already removed) is a benign no-op. */
struct yetty_ycore_void_result yetty_yfigure_container_protect_child(
    struct yetty_yclass_object *obj, uint32_t id);
struct yetty_ycore_void_result yetty_yfigure_container_raise_child_by_id(
    struct yetty_yclass_object *obj, uint32_t id);
struct yetty_yfigure_hit_result yetty_yfigure_container_hit_test(struct yetty_yclass_object *obj,
                                                                 float x, float y);

#ifdef __cplusplus
}
#endif

#endif
