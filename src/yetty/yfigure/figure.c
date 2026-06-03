/*
 * yfigure:figure base-class host TU.
 *
 * `struct yetty_yfigure_figure` is the yclass base class for every
 * concrete figure kind (container, ygrid, ymgui, …). Its layout is
 * PRIVATE to this TU: it is the figure base's yclass data slice, the
 * first slice in every figure object. Concrete kinds inherit via
 * `parent@yfigure:figure`, reach the slice with
 * `yetty_yfigure_figure_data(obj)`, and read/write its fields only
 * through the accessors defined here — figure.h merely forward-
 * declares the struct, so no other module depends on its layout.
 *
 * This TU defines the figure-base polymorphic slots (render, destroy,
 * process_input, process_bytes, reset_content, dump_state) with their
 * default impls; concrete kinds override them. All figure dispatch is
 * through yclass — there is no ops vtable.
 *
 * The codegen-generated accessor for the base class
 * (`yetty_yfigure_figure_class_get`) is included from `figure.gen.c`
 * at the foot.
 */
/* yclass annotation host. The forward declaration must precede the
 * definition (which figure.h brings in) for clang to honour the
 * attribute. Codegen reads the annotation off this decl and attributes
 * the figure class to figure.c — its natural host TU — rather than
 * the first foreign .c that transitively includes the public header. */
struct [[clang::annotate("class@yfigure:figure")]] yetty_yfigure_figure;

#include <yetty/yfigure/figure.h>

/* ---- figure base-class method slots -------------------------------------
 * The figure base defines the polymorphic figure slots; concrete kinds
 * override them. Every slot takes a non-marshallable pointer (target /
 * statemachine) or returns a heap pointer, so each is `local@` — figure
 * dispatch is always in-process. The base defaults model the old
 * NULL-vtable-op semantics: render/reset_content/destroy are no-ops,
 * process_input/process_bytes reject (a kind that wants them overrides),
 * and dump returns NULL so the public wrapper emits its rect fallback. */

[[clang::annotate("override@yfigure:figure:render")]] [[clang::annotate("local@yfigure:render")]]
static struct yetty_ycore_void_result yetty_yfigure_figure_default_render(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ydraw_target *target)
{
    (void)ctx;
    (void)obj;
    (void)target;
    return YETTY_OK_VOID();
}

/* destroy: tears down concrete state and frees the figure. The base
 * default is a no-op — the base class is never instantiated as a leaf;
 * every concrete kind overrides this and frees via object_free. */
[[clang::annotate("override@yfigure:figure:destroy")]] [[clang::annotate("local@yfigure:destroy")]]
static struct yetty_ycore_void_result yetty_yfigure_figure_default_destroy(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    return YETTY_OK_VOID();
}

/* process_input: consume the figure's wire body straight off the SM.
 * Base default rejects — a purely visual figure ignores wire updates.
 * The container only routes here for kinds that override it (capability
 * detected via yetty_yfigure_figure_implements). */
[[clang::annotate("override@yfigure:figure:process_input")]] [[clang::annotate(
    "local@yfigure:process_input")]]
static struct yetty_ycore_void_result yetty_yfigure_figure_default_process_input(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj,
    struct yetty_ywire_wire_statemachine *statemachine)
{
    (void)ctx;
    (void)obj;
    (void)statemachine;
    return YETTY_ERR(yetty_ycore_void, "yfigure: process_input not implemented by this figure");
}

/* process_bytes: apply a buffered wire body. Base default rejects. */
[[clang::annotate("override@yfigure:figure:process_bytes")]] [[clang::annotate(
    "local@yfigure:process_bytes")]]
static struct yetty_ycore_void_result yetty_yfigure_figure_default_process_bytes(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, const uint8_t *bytes,
    size_t bytes_len)
{
    (void)ctx;
    (void)obj;
    (void)bytes;
    (void)bytes_len;
    return YETTY_ERR(yetty_ycore_void, "yfigure: process_bytes not implemented by this figure");
}

/* reset_content: drop content, keep GPU state. Base default rejects so the
 * container falls back to destroy + mint for kinds that don't support it. */
[[clang::annotate("override@yfigure:figure:reset_content")]] [[clang::annotate(
    "local@yfigure:reset_content")]]
static struct yetty_ycore_void_result yetty_yfigure_figure_default_reset_content(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    return YETTY_ERR(yetty_ycore_void, "yfigure: reset_content not implemented by this figure");
}

/* dump_state: heap text snapshot for tests. Base default yields a NULL
 * string so the yetty_yfigure_dump wrapper emits its rect fallback. */
[[clang::annotate("override@yfigure:figure:dump_state")]] [[clang::annotate(
    "local@yfigure:dump_state")]]
static struct yetty_ycore_char_ptr_result yetty_yfigure_figure_default_dump_state(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int indent)
{
    (void)ctx;
    (void)obj;
    (void)indent;
    return YETTY_OK(yetty_ycore_char_ptr, NULL);
}

/* ---- figure base data slice (PRIVATE) -----------------------------------
 * The yclass data slice for the figure base class. It sits first in every
 * figure object (right after the yclass object header), so a kind reaches
 * it with yetty_yfigure_figure_data(obj) — or equivalently the first slice
 * at (object + 1) — and touches its fields only through the accessors
 * below. The layout is intentionally not exported: figure.h forward-
 * declares the struct, nothing else may depend on its fields.
 *
 *   self_obj : owning yclass object header (body sits at self_obj + 1).
 *   rect     : AABB in target pixel space; moves go through the parent's
 *              set_rect so damage tracking stays correct. (No `id` field —
 *              id is a parent-scoped name the container assigns, not a
 *              property of the child.)
 *   z        : stacking order within the parent (higher renders later /
 *              wins hit-tests); ties break on insertion order.
 *   hidden   : parent skips this child entirely (no render, no hit).
 *   dirty    : contents changed without geometry moving; parent ORs this
 *              into its damage region next render pass and clears it. */
struct yetty_yfigure_figure {
    struct yetty_yclass_object *self_obj;
    struct yetty_ycore_rectangle rect;
    int32_t z;
    int hidden;
    int dirty;
};

/* Resolve the figure base slice inside `obj`. The base is the first slice
 * of every figure object, so this is `(object + 1)` typed — but it routes
 * through the yclass data model so a wrong-class object surfaces as an
 * error rather than a bad cast. Kinds that already hold the base handle
 * (their slot wrappers pass `(object + 1)`) can skip this. */
[[clang::annotate("expose")]]
struct yetty_yfigure_figure_ptr_result yetty_yfigure_figure_data(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result cls_r = yetty_yfigure_figure_class_get();
    if (YETTY_IS_ERR(cls_r))
        return YETTY_ERR(yetty_yfigure_figure_ptr, "yfigure_figure_data: class_get", cls_r);
    struct yetty_yclass_void_ptr_result slice_r = yetty_yclass_object_data(obj, cls_r.value);
    if (YETTY_IS_ERR(slice_r))
        return YETTY_ERR(yetty_yfigure_figure_ptr, "yfigure_figure_data: object_data", slice_r);
    return YETTY_OK(yetty_yfigure_figure_ptr, (struct yetty_yfigure_figure *)slice_r.value);
}

/* ---- base-field accessors (the only way to touch the slice) -------------- */

[[clang::annotate("expose")]]
struct yetty_yclass_object *yetty_yfigure_figure_self_obj(const struct yetty_yfigure_figure *fig)
{
    return fig->self_obj;
}

[[clang::annotate("expose")]]
void yetty_yfigure_figure_set_self_obj(struct yetty_yfigure_figure *fig,
                                       struct yetty_yclass_object *self_obj)
{
    fig->self_obj = self_obj;
}

[[clang::annotate("expose")]]
struct yetty_ycore_rectangle yetty_yfigure_figure_rect(const struct yetty_yfigure_figure *fig)
{
    return fig->rect;
}

[[clang::annotate("expose")]]
void yetty_yfigure_figure_set_rect(struct yetty_yfigure_figure *fig,
                                   struct yetty_ycore_rectangle rect)
{
    fig->rect = rect;
}

[[clang::annotate("expose")]]
int32_t yetty_yfigure_figure_z(const struct yetty_yfigure_figure *fig)
{
    return fig->z;
}

[[clang::annotate("expose")]]
void yetty_yfigure_figure_set_z(struct yetty_yfigure_figure *fig, int32_t z)
{
    fig->z = z;
}

[[clang::annotate("expose")]]
int yetty_yfigure_figure_hidden(const struct yetty_yfigure_figure *fig)
{
    return fig->hidden;
}

[[clang::annotate("expose")]]
void yetty_yfigure_figure_set_hidden(struct yetty_yfigure_figure *fig, int hidden)
{
    fig->hidden = hidden;
}

[[clang::annotate("expose")]]
int yetty_yfigure_figure_dirty(const struct yetty_yfigure_figure *fig)
{
    return fig->dirty;
}

[[clang::annotate("expose")]]
void yetty_yfigure_figure_set_dirty(struct yetty_yfigure_figure *fig, int dirty)
{
    fig->dirty = dirty;
}

#ifdef YCLASS_CODEGEN
/* Header-destined content for the generated figure.h (skipped by the real build, which takes it from that header). */
struct yetty_yfigure_figure;
struct yetty_ydraw_target;
struct yetty_ywire_wire_statemachine;

YETTY_YRESULT_DECLARE(yetty_yfigure_figure_ptr, struct yetty_yfigure_figure *);


/*===========================================================================
 * Group — a figure that contains other figures.
 *
 * The group is itself a figure: render iterates children in insertion
 * order (back-to-front), destroy cascades.
 *
 * Children's rects, like every figure's, are in absolute target pixel
 * space. The group is primarily a lifecycle / identity container —
 * destroying a group cascades to its children. The wire decoder is
 * what translates wire-relative coords into the absolute runtime rects
 * as it walks each CMD_GROUP; once decoded, every figure knows its
 * own absolute position. A runtime "move group" helper (when needed)
 * walks descendants to translate their rects.
 *=========================================================================*/

struct yetty_yfigure_container;

YETTY_YRESULT_DECLARE(yetty_yfigure_container_ptr, struct yetty_yfigure_container *);

struct yetty_context;
struct yetty_yfigure_registry;

/* Construction goes through the codegen-emitted yclass factory
 *
 *     yetty_yfigure_container_create(struct yetty_yclass_ctx *ctx)
 *
 * declared in <yetty/yfigure/rpc.h>. Same call on both sides of an
 * RPC session: ctx->session == NULL allocates a local instance and
 * runs the `constructor` slot (sets ops); ctx->session != NULL mints
 * a proxy whose method calls marshal over the session.
 *
 * Per-instance runtime state (rect, context, registry) is set by the
 * owner — the side that hosts the actual container body — via the
 * setters below right after _create. They take a body pointer
 * obtained from `yetty_yfigure_container_from(obj)`. They are NOT
 * yclass slots: the pointers (struct yetty_context *, registry *)
 * are in-process state that doesn't translate across a wire. */

/* Downcast from the yclass header to the typed body. Used by the
 * owner to call the setters below right after _create. NULL in →
 * NULL out. */
struct yetty_yfigure_container *yetty_yfigure_container_from(struct yetty_yclass_object *obj);

/* Owner-side setters for per-instance runtime state. `context` and
 * `registry` are borrowed — the host owns their lifetime; both must
 * outlive every figure they touch. A NULL registry makes any
 * subsequent CREATE_CHILD record fail with "no registry"; that's the
 * right shape for read-only containers. `rect` is the container's
 * own AABB. */
void yetty_yfigure_container_set_registry(struct yetty_yfigure_container *container,
                                          struct yetty_yfigure_registry *registry);

void yetty_yfigure_container_set_context(struct yetty_yfigure_container *container,
                                         const struct yetty_context *context);

void yetty_yfigure_container_set_rect(struct yetty_yfigure_container *container,
                                      struct yetty_ycore_rectangle rect);

/* Consume one full OSC envelope's body off the SM as a record stream
 * targeted at this (root) container. Loops until the SM signals
 * end-of-envelope. This is the per-envelope helper; nested containers
 * use the bounded `process_input` figure op instead, and the wire-SM
 * coro entry below wraps this in a forever loop.
 *
 * After this returns, the container's child set reflects every CREATE/
 * DELETE/UPDATE in the envelope. */
struct yetty_ycore_void_result yetty_yfigure_container_consume_envelope(
    struct yetty_yfigure_container *container, struct yetty_ywire_wire_statemachine *sm);

/* Wire-SM coroutine entry. Pass the container pointer as `userdata`
 * when registering via yetty_ywire_wire_statemachine_register. Loops
 * forever: consume one envelope, yield, repeat — matching the
 * "process_input must loop forever" contract of the wire SM. */
struct yetty_ycore_void_result yetty_yfigure_container_process_input(
    void *userdata, struct yetty_ywire_wire_statemachine *sm);

/* Shift every child rect arriving via admin CREATE_CHILD / SET_CHILD_RECT
 * records by (offset_x, offset_y). Producers emit coords in pane-local
 * space; this offset converts them to target pixel space. Default (0, 0).
 * Doesn't retroactively shift already-bound children. */
void yetty_yfigure_container_set_viewport_offset(struct yetty_yfigure_container *container,
                                                 float offset_x, float offset_y);

/* Direct byte-stream entry — same record decoding as consume_envelope
 * but reads from a contiguous byte array instead of pumping the SM.
 * Used by in-process producers (e.g. yui's embedded ygui_engine) that
 * already hold the serialized record stream. */
struct yetty_ycore_void_result yetty_yfigure_container_process_records(
    struct yetty_yfigure_container *container, const uint8_t *bytes, size_t bytes_len);

/* Upcast: a group is a figure. */
struct yetty_yfigure_figure *yetty_yfigure_container_as_figure(
    struct yetty_yfigure_container *group);

/* Append child at the top of the group's z-order under the given id.
 * The id is parent-scoped — distinct from any other child's id in this
 * group, no global uniqueness required. `id == 0` is reserved (means
 * "no wire address") and rejected; the wire-decode path always supplies
 * a non-zero id read from the producer's envelope.
 *
 * The group takes ownership: the child's yclass destroy slot runs when
 * the group is destroyed or the child is removed by id. */
struct yetty_ycore_void_result yetty_yfigure_container_add_child(
    struct yetty_yfigure_container *group, struct yetty_yfigure_figure *child, uint32_t id);

/* Look up a child by its parent-scoped id. Returns NULL when the id
 * isn't bound. Lookup is O(1) — the group indexes children by id via
 * an internal hash table. */
struct yetty_yfigure_figure *yetty_yfigure_container_find_child_by_id(
    const struct yetty_yfigure_container *group, uint32_t id);

/* Remove and destroy the child bound to `id`. No-op when `id` isn't
 * bound (returns OK_VOID — wire-replay of a stale CMD_DELETE is benign,
 * matches the existing compositor semantics). */
struct yetty_ycore_void_result yetty_yfigure_container_remove_child_by_id(
    struct yetty_yfigure_container *group, uint32_t id);

/* Move the child bound to `id` to the top of the group's z-order
 * (rendered last). No-op when `id` isn't bound. */
struct yetty_ycore_void_result yetty_yfigure_container_raise_child_by_id(
    struct yetty_yfigure_container *group, uint32_t id);

/* Iterate every child in z-order (back-to-front). `fn` is called once
 * per child with its parent-scoped id, figure pointer, and the caller's
 * `user` cookie. Returning non-zero from `fn` stops the walk early and
 * the same value is returned from for_each — callers use that as the
 * "found" / hit short-circuit. Returns 0 when the visitor ran to
 * completion. */
typedef int (*yetty_yfigure_container_visitor_fn)(uint32_t id, struct yetty_yfigure_figure *child,
                                                  void *user);

int yetty_yfigure_container_for_each(struct yetty_yfigure_container *group,
                                     yetty_yfigure_container_visitor_fn fn, void *user);

/*===========================================================================
 * Hit-test against a container's children.
 *
 * The result identifies the child whose rect contains the cursor and
 * the cursor's coordinates inside that child's own pixel space
 * (origin = child rect's top-left). figure_id == 0 means "no hit".
 *
 * Iteration is insertion order = back-to-front; the first match is
 * returned, so for overlapping children this picks the BACK-most.
 * Callers that want top-most-wins should walk children in reverse via
 * for_each + their own state, or this helper can be replaced later
 * once the wire grows an explicit z-order hint.
 *=========================================================================*/

struct yetty_yfigure_hit {
    uint32_t figure_id;
    float local_x;
    float local_y;
};

struct yetty_yfigure_hit yetty_yfigure_container_hit_test(struct yetty_yfigure_container *container,
                                                          float x, float y);

/*===========================================================================
 * Polymorphic dump.
 *
 * `yetty_yfigure_dump` dispatches to the figure's yclass dump_state slot
 * when the concrete kind overrides it; otherwise it returns a one-line
 * fallback with the rect (so tests can still see SOMETHING for unknown kinds).
 * Caller owns the returned string and frees with free(). NULL on OOM.
 *=========================================================================*/
char *yetty_yfigure_dump(const struct yetty_yfigure_figure *self, int indent);
#endif

#include "figure.gen.c"
