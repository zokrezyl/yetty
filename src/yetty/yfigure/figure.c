/*
 * yfigure:figure base-class host TU.
 *
 * `struct yetty_yfigure_figure` is the yclass base class for every
 * concrete figure kind (container, ygrid, ymgui, …). Its layout is
 * PRIVATE to this TU: it is the figure base's yclass data slice, the
 * first slice in every figure object. Concrete kinds inherit via
 * `parent@yfigure:figure` and touch its fields only through the codegen-
 * emitted `property` accessors (yetty_yfigure_figure_<field>_get/_set and
 * the opaque yetty_yfigure_figure_data_get handle); figure.h merely
 * forward-declares the struct, so no other module depends on its layout.
 *
 * This TU defines the figure-base polymorphic slots (render, destroy,
 * process_input, process_bytes, reset_content, dump_state) with their
 * default impls; concrete kinds override them. All figure dispatch is
 * through yclass — there is no ops vtable.
 *
 * The codegen-generated accessor for the base class
 * (`yetty_yfigure_figure_class_get`) is included from `figure.gen.c`
 * at the foot.
 *
 * This TU deliberately does NOT include its own public header
 * `yetty/yfigure/figure.h` — that header is a generated artifact for
 * *other* modules. The foundational types the base impls and the
 * appended `figure.gen.c` need (yclass identity, Result, rectangle) are
 * pulled in directly here, and this TU declares its own
 * `yetty_yfigure_figure_ptr_result` below (the same one figure.h
 * publishes for consumers).
 */

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

/* File-scope forward decls for the types the base impls below take by pointer
 * (default_render's target, default_process_input's statemachine). Needed at
 * file scope so the impl parameter lists refer to these tags rather than
 * minting distinct prototype-scope tags that would mismatch the slot `_fn`
 * typedefs. NOT `expose`d: figure.h's own prototypes never mention these types
 * (render/process_input are declared from container.c into container.h), so
 * emitting them into figure.h would be dead forward-decls. */
struct yetty_ydraw_target;
struct yetty_ywire_wire_statemachine;

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

/* set_scroll: move the figure's scroll offset (the content coordinate shown
 * at the rect's top-left). Base default rejects — a non-scrolling figure has
 * no content larger than its rect. Scrollable kinds (ygrid) override this so
 * the container can drive the offset by id from a wire SET_CHILD_SCROLL
 * record or the terminal's autonomous wheel/key handler, without re-shipping
 * content. */
[[clang::annotate("override@yfigure:figure:set_scroll")]] [[clang::annotate(
    "local@yfigure:set_scroll")]]
static struct yetty_ycore_void_result yetty_yfigure_figure_default_set_scroll(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, float scroll_x, float scroll_y)
{
    (void)ctx;
    (void)obj;
    (void)scroll_x;
    (void)scroll_y;
    return YETTY_ERR(yetty_ycore_void, "yfigure: set_scroll not implemented by this figure");
}

/* set_content_size: declare the figure's content extent in px (the rect stays
 * the visible window; content may be larger, making the figure a scroll
 * viewport). Base default rejects. Scrollable kinds override it. */
[[clang::annotate("override@yfigure:figure:set_content_size")]] [[clang::annotate(
    "local@yfigure:set_content_size")]]
static struct yetty_ycore_void_result yetty_yfigure_figure_default_set_content_size(
    struct yetty_yclass_ctx *ctx, struct yetty_yclass_object *obj, float content_w, float content_h)
{
    (void)ctx;
    (void)obj;
    (void)content_w;
    (void)content_h;
    return YETTY_ERR(yetty_ycore_void, "yfigure: set_content_size not implemented by this figure");
}

/* ---- figure base data slice (PRIVATE) -----------------------------------
 * The yclass data slice for the figure base class — the first slice in every
 * figure object (right after the yclass object header). The layout is not
 * exported: figure.h only forward-declares the struct. Members that other
 * classes legitimately read/write carry a `property` annotation, which makes
 * codegen emit the standardized get/set accessors plus the opaque data-block
 * handle (yetty_yfigure_figure_data_get); unannotated members stay private to
 * this TU. There is deliberately no `self_obj` member — the owning object of
 * a base handle is simply `(struct yetty_yclass_object *)handle - 1`, since
 * the base is always the first slice.
 *
 *   rect   : AABB in target pixel space; moves go through the parent's
 *            set_rect so damage tracking stays correct. (No `id` field — id is
 *            a parent-scoped name the container assigns.)
 *   z      : stacking order within the parent (higher renders later / wins
 *            hit-tests); ties break on insertion order.
 *   hidden : parent skips this child entirely (no render, no hit).
 *   dirty  : contents changed without geometry moving; the parent ORs this
 *            into its damage region next render pass and clears it.
 *   absolute_coords : the figure lays out / hit-tests / paints its content
 *            in pane-root pixel space and only scissor-clips to its rect —
 *            no per-figure re-origin (ygui chrome + scrolling sub-figures).
 *            When 0 (the default for every producer figure: yplot/yimage/…),
 *            content is local to the figure rect. The parent's hit-test reads
 *            this to decide whether to report the cursor in pane-local space
 *            (absolute) or re-origined to the figure rect (local). */
struct [[clang::annotate("class@yfigure:figure")]] yetty_yfigure_figure {
    [[clang::annotate("property")]] struct yetty_ycore_rectangle rect;
    [[clang::annotate("property")]] int z;
    [[clang::annotate("property")]] int hidden;
    [[clang::annotate("property")]] int dirty;
    [[clang::annotate("property")]] int absolute_coords;
};

/* Result wrapper for the figure-base handle. Declared here (not pulled
 * from figure.h, which this TU does not include) so the appended
 * figure.gen.c — which defines yetty_yfigure_figure_from() and the
 * property accessors returning it — has the type in scope. The public
 * figure.h publishes the identical declaration for other modules. */
YETTY_YRESULT_DECLARE(yetty_yfigure_figure_ptr, struct yetty_yfigure_figure *);

#include "figure.gen.c"
