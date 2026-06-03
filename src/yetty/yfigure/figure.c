/*
 * yfigure:figure base-class host TU.
 *
 * `struct yetty_yfigure_figure` is the yclass base class for every
 * concrete figure kind (container, ygrid, ymgui, …). The struct is
 * defined in `<yetty/yfigure/figure.h>` and embedded as the first
 * member by every concrete class; the `parent@yfigure:figure`
 * annotation on each concrete class wires the inheritance to yclass.
 *
 * This TU owns no yclass slots at this stage. Per-figure-kind
 * lifecycle still flows through `struct yetty_yfigure_figure_ops` —
 * an intentionally transitional vtable kept until every concrete kind
 * routes render/destroy/process via yclass dispatch directly.
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
[[clang::annotate("override@yfigure:figure:process_input")]]
[[clang::annotate("local@yfigure:process_input")]]
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
[[clang::annotate("override@yfigure:figure:process_bytes")]]
[[clang::annotate("local@yfigure:process_bytes")]]
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
[[clang::annotate("override@yfigure:figure:reset_content")]]
[[clang::annotate("local@yfigure:reset_content")]]
static struct yetty_ycore_void_result yetty_yfigure_figure_default_reset_content(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj)
{
    (void)ctx;
    (void)obj;
    return YETTY_ERR(yetty_ycore_void, "yfigure: reset_content not implemented by this figure");
}

/* dump_state: heap text snapshot for tests. Base default yields a NULL
 * string so the yetty_yfigure_dump wrapper emits its rect fallback. */
[[clang::annotate("override@yfigure:figure:dump_state")]]
[[clang::annotate("local@yfigure:dump_state")]]
static struct yetty_ycore_char_ptr_result yetty_yfigure_figure_default_dump_state(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, int indent)
{
    (void)ctx;
    (void)obj;
    (void)indent;
    return YETTY_OK(yetty_ycore_char_ptr, NULL);
}

#include "figure.gen.c"

#ifdef YCLASS_CODEGEN
/* Header-destined content for the generated figure.h (skipped by the real build, which takes it from that header). */
struct yetty_yfigure_figure;
struct yetty_ydraw_target;
struct yetty_ywire_wire_statemachine;

YETTY_YRESULT_DECLARE(yetty_yfigure_figure_ptr, struct yetty_yfigure_figure *);

struct yetty_yfigure_figure_ops {
    /* Destroy concrete state and free `self`. For composite figures
     * (groups) this cascades to children. The base struct guarantees
     * `self` is non-NULL when ops are invoked. */
    struct yetty_ycore_void_result (*destroy)(struct yetty_yfigure_figure *self);

    /* Paint into `target`. The figure already knows its position and
     * size from `self->rect` (absolute, target pixel space). It uses
     * `target` polymorphically — view via `target->ops->get_view`,
     * pane via `target->viewport`. The figure owns its own pipeline
     * + binder (yplot-pattern); the concrete kind of target sitting
     * behind the handle doesn't matter to it. */
    struct yetty_ycore_void_result (*render)(struct yetty_yfigure_figure *self,
                                             struct yetty_ydraw_target *target);

    /* Consume input directly from the wire-statemachine. A coroutine —
     * yields when the SM has no bytes ready and resumes when the next
     * chunk arrives. Same shape every figure speaks; no buffered
     * payload, no length argument. The figure reads what it needs from
     * the SM in its own format.
     *
     * For composite figures (yfigure_container), the body is a stream
     * of `{length, id, body}` records — process_input loops reading
     * record headers from the SM and dispatches each to either its own
     * admin handler (id=0) or the matching child's process_input.
     *
     * For leaf figures (ygrid, ymgui, yrdawn, …), the body is the
     * figure-kind's own self-describing format. The figure reads from
     * the SM directly and decodes as it goes.
     *
     * NULL = figure is purely visual and rejects wire updates. */
    struct yetty_ycore_void_result (*process_input)(struct yetty_yfigure_figure *self,
                                                    struct yetty_ywire_wire_statemachine *sm);

    /* Apply a wire update from an in-memory byte buffer. TEMPORARY —
     * the migration target is `process_input` above. Kept while ygrid
     * and the container's admin records still go through the buffered
     * path; will be deleted once every figure kind speaks `process_input`. */
    struct yetty_ycore_void_result (*process_bytes)(struct yetty_yfigure_figure *self,
                                                    const uint8_t *bytes, size_t bytes_len);

    /* Drop the figure's content (prims / record buffer / per-frame
     * scratch) WITHOUT touching its GPU resources (buffers, textures,
     * binder, pipeline). Followed by process_bytes(new_payload), this
     * gives "refresh content, keep GPU state" — the receiver path for
     * CREATE_CHILD on an existing id with the same kind. Without it
     * the only option is destroy + mint, which throws away the binder
     * cache and forces a full pipeline rebuild on the next render
     * (visible as ~100 ms hover lag in yui chrome). NULL = figure
     * doesn't support in-place content reset; CREATE_CHILD on an
     * existing id falls back to destroy + mint. */
    struct yetty_ycore_void_result (*reset_content)(struct yetty_yfigure_figure *self);

    /* Return a heap-allocated text snapshot of this figure's state in a
     * YAML-ish form (no real YAML library is involved — just direct
     * text formatting). Caller frees with free(). NULL on OOM.
     *
     * `indent` is the number of spaces every emitted top-level line
     * should be prefixed with. Composite figures (containers) emit
     * their own fields at `indent`, then recurse into children at
     * `indent + 4` after a `children:` key — the receiver state is
     * a tree, so the dump is too.
     *
     * Used by unit tests to assert receiver state after a wire-byte
     * stream has been processed. NULL = figure provides no dump (the
     * base wrapper returns just the kind + rect). */
    char *(*dump)(const struct yetty_yfigure_figure *self, int indent);
};

/* figure is a concrete yclass base class — concrete kinds (container,
 * ygrid, ymgui, …) inherit via `parent@yfigure:figure`, not via
 * `uses@`. The yclass annotation lives on the forward declaration in
 * `src/yetty/yfigure/figure.c` so the figure module's codegen pass
 * groups the class under figure.c (its natural host TU) rather than
 * the first .c that transitively includes this header. The struct
 * still carries the legacy `yetty_yfigure_figure_ops` vtable head
 * member; that vtable remains intentionally transitional until every
 * concrete figure kind speaks yclass dispatch end-to-end. */
struct yetty_yfigure_figure {
    const struct yetty_yfigure_figure_ops *ops;
    /* The owning yclass object header, set by every yclass-allocated
     * figure right after object_alloc (body sits at object + 1, so this
     * equals `(struct yetty_yclass_object *)figure - 1`). NULL for
     * figures not allocated through yclass (e.g. unit-test mocks that
     * still ride the transitional ops vtable). The container uses this
     * to route render/destroy through yclass dispatch when present and
     * the ops vtable otherwise — the bridge that lets migrated and
     * not-yet-migrated figure kinds coexist. */
    struct yetty_yclass_object *self_obj;
    /* AABB in target pixel space. Set at construction by the concrete
     * figure; subsequent moves go through the parent's set_rect so
     * damage tracking stays correct.
     *
     * NOTE: the figure has no `id` field — id is a parent-scoped name
     * the parent group uses to address its children, not a property of
     * the child itself. See yetty_yfigure_container_add_child. */
    struct yetty_ycore_rectangle rect;
    /* Stacking order within the parent container. Higher z renders
     * later (in front) and wins hit-tests. Default 0. The parent sorts
     * its children by (z, insertion-seq) — equal z falls back to
     * insertion order, so single-z trees behave exactly as before.
     * Set over the wire via the SET_CHILD_Z admin record; coarse bands
     * (chrome < floating windows < menus) keep layers from interleaving. */
    int32_t z;
    /* When set, the parent container skips this child entirely — no
     * render, no hit. Lets a producer hide a figure (e.g. a closed
     * dialog) without deleting it and re-shipping its whole body on the
     * next show. Toggled over the wire via the SET_CHILD_HIDDEN admin
     * record. Default 0 (visible). */
    int hidden;
    /* Set by the figure when its contents change without geometry
     * moving. The parent ORs this into its damage region during the
     * next render pass and clears it after. */
    int dirty;
};

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
 * The group takes ownership: child->ops->destroy runs when the group
 * is destroyed or the child is removed by id. */
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
 * `yetty_yfigure_dump` dispatches to self->ops->dump when present; if the
 * concrete kind doesn't implement dump it returns a one-line fallback
 * with the rect (so tests can still see SOMETHING for unknown kinds).
 * Caller owns the returned string and frees with free(). NULL on OOM.
 *=========================================================================*/
char *yetty_yfigure_dump(const struct yetty_yfigure_figure *self, int indent);
#endif
