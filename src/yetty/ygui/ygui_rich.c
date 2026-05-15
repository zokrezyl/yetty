/*
 * ygui_rich.c — RICH widget.
 *
 * Holds a ypaint-core buffer of pre-built primitives (TEXT_SPAN, SDF
 * shapes, yplot, yimage, ...). At render time every primitive in that
 * buffer is translated by the widget's resolved absolute position and
 * appended to the engine's frame buffer.
 *
 * Authors compose content in widget-local pixel coordinates (0..w, 0..h);
 * the widget handles placement on the canvas. This is how a ygui app
 * embeds GPU-rendered plots, images and styled text inside flex layouts
 * without the author having to know where the box will land.
 *
 * Two constructors:
 *   - rich(engine, id, x, y, w, h)            empty surface, fill later
 *   - rich_from_yaml(... yaml, yaml_len)      convenience: parses YAML via
 *                                              ypaint-yaml and hands the
 *                                              resulting buffer to the
 *                                              widget (equivalent to
 *                                              rich() + set_yaml())
 *
 * The two helpers (create + create-from-yaml) match the user request in
 * #ygreeter — the YAML form is a thin wrapper that defers to the empty
 * constructor and then loads the buffer.
 */

#include "ygui_internal.h"

#include <yetty/ydraw-core/buffer.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/ysdf/types.gen.h>

#include <yetty/ytrace/ytrace.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* set_yaml lives in ygui_rich_yaml.c (separate TU so the shared-lib
 * variant of ygui can skip the YAML / libyaml / fontconfig dep). */

/*=============================================================================
 * Buffer walker — sized strides without depending on a flyweight registry.
 *
 * The ypaint wire format reserves type-id ranges per category:
 *   [0x00000000, 0x0000FFFF]  cmd   FAM:  type, payload_size, payload[]
 *   [0x10000000, 0x1FFFFFFF]  SDF   plain: type, z_order, fill, stroke,
 *                                          stroke_width, args[]    (sized
 *                                          via yetty_ysdf_primitive_size)
 *   [0x40000000, 0x7FFFFFFF]  flyweight (FONT, TEXT_SPAN)  FAM
 *   [0x80000000, 0xFFFFFFFF]  complex (yplot, yimage, ...) FAM
 *
 * For FAM prims size = 8 + payload_size (payload_size already 4-aligned).
 * For SDF prims size comes from the auto-generated word-count table.
 *===========================================================================*/

static size_t rich_prim_size(const uint32_t *prim, size_t remaining)
{
    if (remaining < sizeof(uint32_t)) {
        return 0;
    }
    uint32_t type = prim[0];

    /* SDF tier */
    if (type >= 0x10000000u && type < 0x20000000u) {
        size_t s = yetty_ysdf_primitive_size(type);
        return (s > 0 && s <= remaining) ? s : 0;
    }

    /* CMD / flyweight / complex — all FAM. */
    if (remaining < 2 * sizeof(uint32_t)) {
        return 0;
    }
    uint32_t payload_size = prim[1];
    size_t s = 2 * sizeof(uint32_t) + payload_size;
    return (s <= remaining) ? s : 0;
}

/*=============================================================================
 * Per-type position translation.
 *
 * For each primitive category we identify the float indices (relative to
 * the start of the prim) that hold canvas X/Y coordinates, and add dx/dy
 * in place.
 *
 * SDF prims share the prefix [type:u32, z_order:u32, fill:u32, stroke:u32,
 * stroke_width:f32, args...] — i.e. floats 5..N are the geometry args.
 * The handful of prims we actually need below have center_x/center_y at
 * floats 5/6; segment has start+end at 5/6, 7/8; triangle has 3 vertex
 * pairs at 5..10; the gradient boxes carry an extra gradient axis.
 *
 * Unknown SDF types fall back to translating floats 5 and 6 only, which
 * is the convention for every center-anchored shape — wrong is better
 * than missing for those (worst case: a yet-untranslated field), and
 * adding a new case to this switch is cheap.
 *===========================================================================*/

static void translate_sdf(uint32_t *prim, size_t words, float dx, float dy)
{
    if (words < 7) {
        return; /* type+z+fill+stroke+sw+x+y minimum */
    }
    uint32_t type = prim[0];

    /* Every SDF prim carries center/start at float indices 5, 6. */
    float *fprim = (float *)prim;
    fprim[5] += dx;
    fprim[6] += dy;

    switch (type) {
    case YETTY_YSDF_SEGMENT:
        if (words >= 9) {
            fprim[7] += dx;
            fprim[8] += dy;
        }
        break;
    case YETTY_YSDF_TRIANGLE:
        if (words >= 11) {
            fprim[7] += dx;
            fprim[8] += dy;
            fprim[9] += dx;
            fprim[10] += dy;
        }
        break;
    case YETTY_YSDF_LINEAR_GRADIENT_BOX:
        /* args: center_x, center_y, half_w, half_h, corner, gx0, gy0,
         * gx1, gy1, color0, color1 → gradient endpoints at floats 10..13. */
        if (words >= 14) {
            fprim[10] += dx;
            fprim[11] += dy;
            fprim[12] += dx;
            fprim[13] += dy;
        }
        break;
    case YETTY_YSDF_RADIAL_GRADIENT_BOX:
        /* gradient center at floats 10..11. */
        if (words >= 12) {
            fprim[10] += dx;
            fprim[11] += dy;
        }
        break;
    default:
        break;
    }
}

static void translate_text_span(uint32_t *prim, size_t words, float dx, float dy)
{
    /* FAM layout: [type, payload_size, x, y, font_size, rotation, color, ...] */
    if (words < 4) {
        return;
    }
    float *fprim = (float *)prim;
    fprim[2] += dx;
    fprim[3] += dy;
}

static void translate_complex(uint32_t *prim, size_t words, float dx, float dy)
{
    /* FAM layout: [type, payload_size, bounds_x, bounds_y, bounds_w, bounds_h, ...] */
    if (words < 4) {
        return;
    }
    float *fprim = (float *)prim;
    fprim[2] += dx;
    fprim[3] += dy;
}

static void translate_prim(uint32_t *prim, size_t bytes, float dx, float dy)
{
    if (bytes < sizeof(uint32_t)) {
        return;
    }
    uint32_t type = prim[0];
    size_t words = bytes / sizeof(uint32_t);

    if (type < 0x00010000u) {
        /* CMD — payload-less in practice (CMD_ZERO); nothing to translate. */
        return;
    }
    if (type >= 0x10000000u && type < 0x20000000u) {
        translate_sdf(prim, words, dx, dy);
        return;
    }
    if (type == YETTY_YDRAW_TYPE_TEXT_SPAN) {
        translate_text_span(prim, words, dx, dy);
        return;
    }
    if (type >= 0x80000000u) {
        translate_complex(prim, words, dx, dy);
        return;
    }
    /* FONT (0x40000001) and any other flyweight without a position —
     * leave untouched. */
}

/*=============================================================================
 * Render: translate each prim in the source buffer by the widget's
 * absolute layout box and append it to the engine's frame buffer.
 *
 * The source buffer's raw primitive byte stream is exposed by
 * yetty_ydraw_core_buffer_data() / _size() — these used to be
 * module-private; promoted to the public surface for producers that
 * need to walk their own buffers (this widget is the motivating case).
 *===========================================================================*/

static struct yetty_ycore_void_result rich_render(struct yetty_ygui_widget *self,
                                                  struct yetty_ygui_render_ctx *ctx)
{
    ydebug("rich_render enter id=%s buf=%p ctx=%p ctx_buf=%p",
           self->id ? self->id : "?", (void *)self->data.rich.buffer, (void *)ctx,
           ctx ? (void *)ctx->buffer : NULL);
    if (!self->data.rich.buffer || !ctx || !ctx->buffer) {
        ydebug("rich_render bail (null) id=%s", self->id ? self->id : "?");
        return YETTY_OK_VOID();
    }
    const uint8_t *src_data =
        (const uint8_t *)yetty_ydraw_core_buffer_data(self->data.rich.buffer);
    size_t src_size = yetty_ydraw_core_buffer_size(self->data.rich.buffer);
    ydebug("rich_render id=%s src=%p size=%zu layout=(%.1f,%.1f)",
           self->id ? self->id : "?", (const void *)src_data, src_size,
           self->layout_x, self->layout_y);
    if (!src_data || src_size == 0) {
        return YETTY_OK_VOID();
    }

    float dx = self->layout_x;
    float dy = self->layout_y;

    const uint8_t *p = src_data;
    size_t remaining = src_size;
    /* Working buffer reused across prims (max prim size in this tier is
     * a few hundred bytes; allocate generously and reuse). */
    uint8_t stack[4096];
    uint8_t *heap = NULL;
    size_t heap_cap = 0;

    int n_prims = 0;
    while (remaining >= sizeof(uint32_t)) {
        size_t s = rich_prim_size((const uint32_t *)p, remaining);
        uint32_t t = ((const uint32_t *)p)[0];
        ydebug("rich_render prim#%d type=0x%x size=%zu rem=%zu", n_prims, t, s, remaining);
        if (s == 0 || s > remaining) {
            ydebug("rich_render break — malformed/zero size");
            break; /* malformed — bail rather than risk overruns */
        }
        uint8_t *work = stack;
        if (s > sizeof(stack)) {
            if (s > heap_cap) {
                uint8_t *grown = (uint8_t *)realloc(heap, s);
                if (!grown) {
                    free(heap);
                    return YETTY_ERR(yetty_ycore_void, "rich_render: oom");
                }
                heap = grown;
                heap_cap = s;
            }
            work = heap;
        }
        memcpy(work, p, s);
        translate_prim((uint32_t *)work, s, dx, dy);
        struct yetty_ydraw_core_id_result r =
            yetty_ydraw_core_buffer_add_prim(ctx->buffer, work, s);
        if (YETTY_IS_ERR(r)) {
            free(heap);
            return YETTY_ERR(yetty_ycore_void, "rich_render: add_prim failed", r);
        }
        p += s;
        remaining -= s;
        n_prims++;
    }
    ydebug("rich_render exit id=%s n_prims=%d remaining=%zu",
           self->id ? self->id : "?", n_prims, remaining);
    free(heap);
    return YETTY_OK_VOID();
}

static void rich_destroy(struct yetty_ygui_widget *self)
{
    if (self->data.rich.buffer) {
        yetty_ydraw_core_buffer_destroy(self->data.rich.buffer);
        self->data.rich.buffer = NULL;
    }
}

/*=============================================================================
 * Constructors / setters
 *===========================================================================*/

/* widget_alloc / widget_init_base are declared in ygui_internal.h.
 * The engine-list insertion in ygui_widgets.c uses a file-static
 * add_to_engine(); we re-expose it via yetty_ygui_engine_attach_widget so
 * the new widget files (this one, ygui_tabbar.c) don't have to duplicate
 * linked-list bookkeeping. */
void yetty_ygui_engine_attach_widget(struct yetty_ygui_engine *engine,
                                     struct yetty_ygui_widget *widget);

static const struct yetty_ygui_widget_vtable rich_vtable = {
    .render = rich_render,
    .destroy = rich_destroy,
};

struct yetty_ygui_widget *yetty_ygui_engine_rich(struct yetty_ygui_engine *engine, const char *id,
                                                 float x, float y, float w, float h)
{
    struct yetty_ygui_widget *r =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_RICH, id);
    if (!r) {
        return NULL;
    }
    yetty_ygui_widget_init_base(r, x, y, w, h);
    r->data.rich.buffer = NULL;
    r->vtable = &rich_vtable;
    yetty_ygui_engine_attach_widget(engine, r);
    return r;
}

/* yetty_ygui_engine_rich_from_yaml lives in ygui_rich_yaml.c — see top
 * of this file for the rationale. */

void yetty_ygui_widget_rich_set_buffer(struct yetty_ygui_widget *widget,
                                       struct yetty_ydraw_core_buffer *buffer)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_RICH) {
        return;
    }
    if (widget->data.rich.buffer && widget->data.rich.buffer != buffer) {
        yetty_ydraw_core_buffer_destroy(widget->data.rich.buffer);
    }
    widget->data.rich.buffer = buffer;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

void yetty_ygui_widget_rich_clear(struct yetty_ygui_widget *widget)
{
    yetty_ygui_widget_rich_set_buffer(widget, NULL);
}

/* rich_set_yaml + the YAML-using parts of rich_from_yaml live in
 * ygui_rich_yaml.c. Keeping them out of this TU lets the libygui.so
 * variant link without pulling libyaml / fontconfig transitively. */
