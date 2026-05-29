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

#include "figure.gen.c"
