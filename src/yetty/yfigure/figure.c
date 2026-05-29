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

#include "figure.gen.c"
