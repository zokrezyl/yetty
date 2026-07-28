/*
 * primitive-widget.c — chrome-widget base class.
 *
 * Subclass of the base widget. Overrides emit_body so paint() does
 * the heavy lifting — chrome widget subclasses (label, button, panel,
 * hbox, vbox, tabbar, tooltip) only need to override paint to write
 * their SDF / glyph prim records into ctx->ygrid_drawable_list.
 *
 * NOTE on CMD_GROUP: the widget.h header documents an emit_body that
 * wraps paint() in CMD_GROUP(obj->id, rect) so each widget's prims are
 * entity-scoped. The current ygrid receiver, however, has no exact-
 * match disambiguation for CMD_GROUP (type=0x80000002): the type word
 * falls into the composite range [0x80000000, 0xffffffff] and is
 * routed to the composite handler, which reads the next u32 as
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

#include <yetty/ydraw-core/drawable-list.h>
#include <yetty/ygui/widget.h>
#include <yetty/ysdf/funcs.gen.h>

/* This TU deliberately does NOT include its own generated header
 * `yetty/ygui/primitive-widget.h` — that header is a downstream artifact
 * for other modules. The parent `widget.h` (pulled in via internal.h) and
 * the foundational yclass / result / types headers carry everything the
 * impls need. The class-handle Result wrapper and the obj→slice downcast
 * the appended primitive-widget.gen.c defines are declared just after the
 * class struct below; the public primitive-widget.h publishes the
 * identical declarations for consumers. */

/* Marker data struct — primitive_widget adds no per-instance fields
 * (it's a chrome-widget base), but yclass codegen needs a `class@`
 * annotation to sit on something. The struct's size contributes 1
 * byte to the instance layout, which is harmless. */
struct YETTY_ANNOTATE("class@ygui:primitive_widget") YETTY_ANNOTATE("parent@ygui:widget")
    yetty_ygui_primitive_widget {
    char unused;
};

/* Result wrapper for the primitive_widget data slice + the codegen
 * downcast/accessor the appended primitive-widget.gen.c defines.
 * Declared here (not pulled from primitive-widget.h, which this TU does
 * not include) so the foot include has them in scope. */
YETTY_YRESULT_DECLARE(yetty_ygui_primitive_widget_ptr, struct yetty_ygui_primitive_widget *);
struct yetty_yclass_ptr_result yetty_ygui_primitive_widget_class_get(void);
struct yetty_ygui_primitive_widget_ptr_result yetty_ygui_primitive_widget_from(
    struct yetty_yclass_object *obj);

YETTY_ANNOTATE("override@ygui:primitive_widget:widget_emit_body")
static struct yetty_ycore_void_result primitive_emit_body(struct yetty_yclass_object *yclass_obj,
                                                          struct yetty_ygui_emit_ctx *ctx)
{
    struct yetty_yclass_object *obj = (struct yetty_yclass_object *)yclass_obj;
    /* Optional background fill (set via yetty_ygui_widget_set_bg_color),
     * painted under the widget's own paint. Skipped when transparent so
     * widgets that never set a bg are unchanged. */
    struct yetty_ycore_uint32_result bg_res = yetty_ygui_widget_bg(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, bg_res, "primitive_emit_body: bg");
    uint32_t bg = bg_res.value;
    if (ctx && ctx->ygrid_drawable_list && (bg >> 24) != 0u) {
        struct yetty_ycore_rectangle_result rect_res = yetty_ygui_widget_rect(obj);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rect_res, "primitive_emit_body: rect");
        struct yetty_ycore_rectangle r = rect_res.value;
        float w = r.max.x - r.min.x, h = r.max.y - r.min.y;
        if (w > 0.0f && h > 0.0f) {
            struct yetty_ysdf_box geom = {.center_x = r.min.x + w * 0.5f,
                                          .center_y = r.min.y + h * 0.5f,
                                          .half_width = w * 0.5f,
                                          .half_height = h * 0.5f,
                                          .corner_radius = 0.0f};
            struct yetty_ycore_void_result br = yetty_ydraw_drawable_list_add_cmd_add_box(
                ctx->ygrid_drawable_list, 0, 0, bg, 0, 0.0f, &geom);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, br, "primitive_emit_body: bg");
        }
    }
    return yetty_ygui_widget_paint(yclass_obj, ctx);
}

#include "yetty/gen/impl/ygui/primitive-widget.c"
