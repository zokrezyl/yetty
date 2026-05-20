/*
 * ygui_rich.c — RICH widget.
 *
 * Holds a ydraw-core buffer of pre-built primitives (TEXT_SPAN, SDF
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
 *                                              ydraw-yaml and hands the
 *                                              resulting buffer to the
 *                                              widget (equivalent to
 *                                              rich() + set_yaml())
 *
 * The two helpers (create + create-from-yaml) match the user request in
 * #ygreeter — the YAML form is a thin wrapper that defers to the empty
 * constructor and then loads the buffer.
 */

#include "ygui_internal.h"

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/text-span-prim.h>
#include <yetty/ysdf/types.gen.h>

/* Strip CMD_GROUP / CMD_DELETE / addressable-record HAS_ID bit so the
 * raw type word can be matched against the SDF type table. */
#define RICH_TYPE_BASE(t) ((uint32_t)(t) & ~YETTY_YDRAW_HAS_ID_FLAG)

#include <yetty/ytrace/ytrace.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* set_yaml lives in ygui_rich_yaml.c (separate TU so the shared-lib
 * variant of ygui can skip the YAML / libyaml / fontconfig dep). */

/*=============================================================================
 * Buffer walker — sized strides without depending on a flyweight registry.
 *
 * The ydraw wire format actually reserves these type-id ranges (see
 * include/yetty/ydraw-core/cmds.h, include/yetty/ydraw-core/font-prim.h,
 * include/yetty/ydraw-core/text-span-prim.h, include/yetty/ysdf/types.gen.h):
 *   [0x00000000, 0x0000FFFF]  cmd       FAM:  type, payload_size, payload[]
 *   0x40000001                FONT      FAM
 *   0x40000002                TEXT_SPAN FAM
 *   [0x7FFFFF7C, 0x7FFFFFFF]  SDF       fixed word count per type
 *                                       (yetty_ysdf_primitive_size)
 *   [0x80000000, 0xFFFFFFFF]  complex   FAM (yplot, yimage, ...)
 *
 * SDFs may also carry the HAS_ID flag (0x80000000 OR'd onto the type
 * word) — addressable variant inserts a 4-byte id right after the type
 * word, adding 4 bytes to the prim's overall size.
 *
 * For FAM prims size = 8 + payload_size (payload_size already 4-aligned).
 * For SDF prims size comes from the auto-generated word-count table.
 *===========================================================================*/

static size_t rich_drawable_size(const uint32_t *prim, size_t remaining)
{
    if (remaining < sizeof(uint32_t)) {
        return 0;
    }
    uint32_t type = prim[0];

    /* SDF tier — primitive_size returns 0 for any non-SDF type word, so
     * it doubles as the type-class probe. Mask off the optional HAS_ID
     * bit before the lookup; if addressable, add the trailing id slot. */
    uint32_t base = RICH_TYPE_BASE(type);
    size_t sdf_bytes = yetty_ysdf_primitive_size(base);
    if (sdf_bytes > 0) {
        size_t s = sdf_bytes + ((type & YETTY_YDRAW_HAS_ID_FLAG) ? sizeof(uint32_t) : 0);
        return (s <= remaining) ? s : 0;
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
    /* Layout (non-addressable):
     *   [type, z, fill, stroke, sw, x, y, ...] — geometry starts at float 5.
     * Addressable variant shifts every field by one slot to make room for
     * the trailing id word inserted right after type. Handle both. */
    uint32_t type = prim[0];
    uint32_t base = RICH_TYPE_BASE(type);
    size_t shift = (type & YETTY_YDRAW_HAS_ID_FLAG) ? 1u : 0u;
    size_t geom = 5u + shift; /* index of center_x / start_x */

    if (words < geom + 2u) {
        return;
    }
    float *fprim = (float *)prim;
    fprim[geom + 0] += dx;
    fprim[geom + 1] += dy;

    switch (base) {
    case YETTY_YSDF_SEGMENT:
        if (words >= geom + 4u) {
            fprim[geom + 2] += dx;
            fprim[geom + 3] += dy;
        }
        break;
    case YETTY_YSDF_TRIANGLE:
        if (words >= geom + 6u) {
            fprim[geom + 2] += dx;
            fprim[geom + 3] += dy;
            fprim[geom + 4] += dx;
            fprim[geom + 5] += dy;
        }
        break;
    case YETTY_YSDF_LINEAR_GRADIENT_BOX:
        /* args: cx, cy, hw, hh, corner, gx0, gy0, gx1, gy1, color0, color1.
         * Gradient endpoints are at geom+5 .. geom+8. */
        if (words >= geom + 9u) {
            fprim[geom + 5] += dx;
            fprim[geom + 6] += dy;
            fprim[geom + 7] += dx;
            fprim[geom + 8] += dy;
        }
        break;
    case YETTY_YSDF_RADIAL_GRADIENT_BOX:
        /* args end with gradient center → geom+5 .. geom+6. */
        if (words >= geom + 7u) {
            fprim[geom + 5] += dx;
            fprim[geom + 6] += dy;
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
    /* SDF — primitive_size doubles as the type-class probe (returns 0
     * for non-SDF). Check before the FAM/complex branches because SDF
     * types overlap the [0x40000000, 0x7FFFFFFF] flyweight range. */
    if (yetty_ysdf_primitive_size(RICH_TYPE_BASE(type)) > 0u) {
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
 * Widget-local clip — drops primitives whose anchor falls outside the rich
 * widget's resolved layout box.
 *
 * Producers like yzoo / yjungle can emit primitives at scene coordinates
 * outside their declared scene_w × scene_h (e.g. yzoo's drift cull is keyed
 * on an unclamped margin while the drawn marker is clamped to 12 px, so a
 * control point can sit dozens of pixels off the scene before being culled
 * — and its marker, plus any curve segment passing through it, paints
 * outside). Without a real scissor stage in the WGSL pipeline, the only
 * deterministic guard the rich widget can offer is "drop primitives that
 * don't anchor inside my box".
 *
 * The anchor is the primitive's position field as documented for translate_*
 * above (SDF center for fixed-size SDFs, payload[2..3] for FAM/complex).
 * Returns 0 for primitive types that have no usable position (CMD, FONT) —
 * the caller keeps those.
 *===========================================================================*/

static int prim_local_anchor(const uint32_t *prim, size_t bytes, float *out_x, float *out_y)
{
    if (bytes < sizeof(uint32_t)) {
        return 0;
    }
    uint32_t type = prim[0];
    size_t words = bytes / sizeof(uint32_t);
    const float *fprim = (const float *)prim;

    if (type < 0x00010000u) {
        return 0;
    }
    if (yetty_ysdf_primitive_size(RICH_TYPE_BASE(type)) > 0u) {
        size_t shift = (type & YETTY_YDRAW_HAS_ID_FLAG) ? 1u : 0u;
        size_t geom = 5u + shift;
        if (words < geom + 2u) {
            return 0;
        }
        /* For SDF_SEGMENT geom is the start point; the end point lives at
         * geom+2/geom+3. Use the midpoint so a segment with one endpoint
         * inside survives. */
        if (RICH_TYPE_BASE(type) == YETTY_YSDF_SEGMENT && words >= geom + 4u) {
            *out_x = 0.5f * (fprim[geom + 0] + fprim[geom + 2]);
            *out_y = 0.5f * (fprim[geom + 1] + fprim[geom + 3]);
            return 1;
        }
        *out_x = fprim[geom + 0];
        *out_y = fprim[geom + 1];
        return 1;
    }
    if (type == YETTY_YDRAW_TYPE_TEXT_SPAN) {
        if (words < 4) {
            return 0;
        }
        *out_x = fprim[2];
        *out_y = fprim[3];
        return 1;
    }
    if (type >= 0x80000000u) {
        /* Complex prim: payload[2..3] is bounds_x/y, payload[4..5] usually
         * bounds_w/h — anchor on the bbox centre when present, else on the
         * top-left. */
        if (words >= 6) {
            *out_x = fprim[2] + 0.5f * fprim[4];
            *out_y = fprim[3] + 0.5f * fprim[5];
            return 1;
        }
        if (words >= 4) {
            *out_x = fprim[2];
            *out_y = fprim[3];
            return 1;
        }
        return 0;
    }
    /* FONT flyweight — no position. */
    return 0;
}

/*=============================================================================
 * Render: translate each prim in the source buffer by the widget's
 * absolute layout box and append it to the engine's frame buffer.
 *
 * The source buffer's raw primitive byte stream is exposed by
 * yetty_ydraw_draw_list_data() / _size() — these used to be
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
        (const uint8_t *)yetty_ydraw_draw_list_data(self->data.rich.buffer);
    size_t src_size = yetty_ydraw_draw_list_size(self->data.rich.buffer);
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

    float lw = self->layout_w;
    float lh = self->layout_h;

    int n_prims = 0;
    while (remaining >= sizeof(uint32_t)) {
        size_t s = rich_drawable_size((const uint32_t *)p, remaining);
        uint32_t t = ((const uint32_t *)p)[0];
        ydebug("rich_render prim#%d type=0x%x size=%zu rem=%zu", n_prims, t, s, remaining);
        if (s == 0 || s > remaining) {
            ydebug("rich_render break — malformed/zero size");
            break; /* malformed — bail rather than risk overruns */
        }

        /* Widget-local clip. Producer-emitted primitives anchored outside
         * the rich widget's resolved box are dropped before they get
         * translated onto the canvas. Without this guard, yzoo / yjungle
         * etc. can paint over neighbouring widgets (tabbar, menubar) any
         * time their internal cull/clip is looser than the actual drawn
         * extent. See prim_local_anchor for the per-type anchor rule. */
        float ax = 0.0f, ay = 0.0f;
        if (prim_local_anchor((const uint32_t *)p, s, &ax, &ay)) {
            if (ax < 0.0f || ax > lw || ay < 0.0f || ay > lh) {
                ydebug("rich_render drop prim#%d type=0x%x anchor=(%.1f,%.1f) box=(%.1fx%.1f)",
                       n_prims, t, ax, ay, lw, lh);
                p += s;
                remaining -= s;
                continue;
            }
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
        struct yetty_ydraw_id_result r =
            yetty_ydraw_draw_list_add_prim(ctx->buffer, work, s);
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
        yetty_ydraw_draw_list_destroy(self->data.rich.buffer);
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
                                       struct yetty_ydraw_draw_list *buffer)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_RICH) {
        return;
    }
    if (widget->data.rich.buffer && widget->data.rich.buffer != buffer) {
        yetty_ydraw_draw_list_destroy(widget->data.rich.buffer);
    }
    widget->data.rich.buffer = buffer;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

void yetty_ygui_widget_rich_clear(struct yetty_ygui_widget *widget)
{
    yetty_ygui_widget_rich_set_buffer(widget, NULL);
}

/* rich_set_yaml + the YAML-using parts of rich_from_yaml live in
 * ygui_rich_yaml.c. Keeping them out of this TU lets the libygui.so
 * variant link without pulling libyaml / fontconfig transitively. */
