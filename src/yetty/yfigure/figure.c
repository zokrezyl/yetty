/*
 * yfigure:figure mixin host TU.
 *
 * The mixin's annotation lives on `struct yetty_yfigure_figure` in
 * `<yetty/yfigure/figure.h>` — embedded by every figure-kind class
 * verbatim. This TU exists so codegen has a `.c` source to walk for
 * the figure module's annotated declarations; it carries no impl of
 * its own.
 *
 * The mixin owns no yclass slots at this stage. Per-figure-kind
 * lifecycle is expressed through:
 *   - the kind's own class slots (process_bytes, clear, set_font, …)
 *   - the existing `struct yetty_yfigure_figure_ops` vtable, kept in
 *     parallel until every caller switches to yclass dispatch.
 *
 * Both axes coexist so the cutover can happen one kind at a time
 * without breaking the build mid-flight.
 */
#include <yetty/yfigure/figure.h>

#include "figure.gen.c"
