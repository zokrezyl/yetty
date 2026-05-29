/*
 * primitive-widget.c — chrome-widget base class.
 *
 * Subclass of the base widget. Overrides emit_body so paint() does
 * the heavy lifting — chrome widget subclasses (label, button, panel,
 * hbox, vbox, tabbar, tooltip) only need to override paint to write
 * their SDF / glyph prim records into ctx->ygrid_draw_list.
 *
 * NOTE on CMD_GROUP: the widget.h header documents an emit_body that
 * wraps paint() in CMD_GROUP(obj->id, rect) so each widget's prims are
 * entity-scoped. The current ygrid receiver, however, has no exact-
 * match disambiguation for CMD_GROUP (type=0x80000002): the type word
 * falls into the complex-prim range [0x80000000, 0xffffffff] and is
 * routed to the complex-prim handler, which reads the next u32 as
 * `payload_size`, advances the stride wrong, and the rest of the
 * stream becomes garbage. Until the ygrid iterator special-cases the
 * three cmd constants (CMD_GROUP / CMD_DELETE / CMD_UPDATE) the way
 * cmds.h says it should, primitive_emit_body must keep the prims
 * anonymous — that's full-redraw-only (no per-widget invalidation)
 * but it's what the receiver actually parses today.
 *
 * Figure widgets (yimage, yplot, …) do NOT inherit from this class —
 * they extend the base widget directly and override both emit_container
 * and emit_body themselves.
 */

#include "internal.h"

#include <yetty/ygui/primitive-widget.h>

/* Marker data struct — primitive_widget adds no per-instance fields
 * (it's a chrome-widget base), but yclass codegen needs a `class@`
 * annotation to sit on something. The struct's size contributes 1
 * byte to the instance layout, which is harmless. */
struct [[clang::annotate("class@ygui:primitive_widget")]] [[clang::annotate("parent@ygui:widget")]]
primitive_widget_data {
    char _empty;
};

[[clang::annotate("override@ygui:primitive_widget:widget_emit_body")]]
static struct yetty_ycore_void_result primitive_emit_body(struct yetty_yclass_ctx *yclass_ctx,
                                                          struct yetty_yclass_object *yclass_obj,
                                                          struct yetty_ygui_emit_ctx *ctx)
{
    (void)yclass_ctx;
    return yetty_ygui_widget_paint(NULL, yclass_obj, ctx);
}

#include "primitive-widget.gen.c"
