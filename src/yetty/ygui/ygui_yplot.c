/*
 * ygui_yplot.c — dedicated yplot widget.
 *
 * Owns its source (yexpr-plot text) + optional data buffers + render
 * config; rebuilds the yplot prim from the current resolved layout
 * box on every render where the box changed. Same pattern as
 * ygui_yimage / ygui_yvideo — no piggyback on rich.
 */

#include "ygui_internal.h"
#include <yetty/yfigure/wire.h>

#include <stdlib.h>
#include <string.h>

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yplot.h>
#include <yetty/yplot/yplot.h>
#include <yetty/ytrace/ytrace.h>

/* Forward decl — same pattern ygui_rich.c uses. */
void yetty_ygui_engine_attach_widget(struct yetty_ygui_engine *engine,
                                     struct yetty_ygui_widget *widget);

/*=============================================================================
 * State management
 *===========================================================================*/

static void yplot_invalidate_cache(struct yetty_ygui_widget *self)
{
    if (self->data.yplot.cached) {
        yetty_ydraw_draw_list_destroy(self->data.yplot.cached);
        self->data.yplot.cached = NULL;
    }
    self->data.yplot.last_w = 0.0f;
    self->data.yplot.last_h = 0.0f;
}

static void yplot_free_buffers(struct yetty_ygui_widget *self)
{
    if (!self->data.yplot.buffers) {
        return;
    }
    for (size_t i = 0; i < self->data.yplot.buffer_count; i++) {
        free(self->data.yplot.buffers[i].samples);
    }
    free(self->data.yplot.buffers);
    self->data.yplot.buffers = NULL;
    self->data.yplot.buffer_count = 0;
}

static struct yetty_ydraw_draw_list *yplot_build_buffer(struct yetty_ygui_widget *self, float w,
                                                        float h)
{
    if (w <= 0.0f || h <= 0.0f) {
        return NULL;
    }
    struct yetty_yplot_render_config cfg = {0};
    if (self->data.yplot.has_cfg) {
        cfg = self->data.yplot.cfg;
    }
    /* Bounds always follow the resolved layout box — the caller's
     * config bounds are ignored. That's the whole point of being a
     * widget: layout decides the size, not the caller. */
    /* CLIENT-AREA INVARIANT: yplot is a single complex prim whose
     * (bounds_x, bounds_y) anchors it on the canvas and (bounds_w,
     * bounds_h) defines its extent. The widget always authors the prim
     * in WIDGET-LOCAL coordinates — top-left at (0, 0), extent exactly
     * (w, h) where (w, h) is the resolved layout box. Composition
     * (emit_buffer_translated) then adds the widget's absolute
     * (layout_x, layout_y) to (bounds_x, bounds_y) so the wire carries
     * absolute canvas coords matching the widget's client area exactly.
     * No part of the prim can render outside that rectangle — the
     * fragment shader (yplot.wgsl: `if (local_pos < 0 || local_pos >=
     * bounds_*) discard`) enforces it. */
    cfg.bounds_x = 0.0f;
    cfg.bounds_y = 0.0f;
    cfg.bounds_w = w;
    cfg.bounds_h = h;
    ydebug("yplot_build_buffer id=%s WIDGET-LOCAL bounds=(x=%.1f y=%.1f w=%.1f h=%.1f)",
           self->id ? self->id : "?", cfg.bounds_x, cfg.bounds_y, cfg.bounds_w, cfg.bounds_h);

    const char *src = self->data.yplot.source ? self->data.yplot.source : "";
    size_t src_len = self->data.yplot.source_len;
    if (self->data.yplot.source && src_len == 0u) {
        src_len = strlen(src);
    }

    struct yetty_ydraw_draw_list_result r;
    if (self->data.yplot.buffers && self->data.yplot.buffer_count > 0) {
        /* Materialise the wire-format buffer_input array yplot expects.
         * Stack-allocate up to a reasonable count; spill to heap above. */
        enum { STACK_BUFS = 16 };
        struct yetty_yplot_buffer_input stack[STACK_BUFS];
        struct yetty_yplot_buffer_input *bufs = stack;
        int heap = 0;
        if (self->data.yplot.buffer_count > STACK_BUFS) {
            bufs = (struct yetty_yplot_buffer_input *)calloc(
                self->data.yplot.buffer_count, sizeof(struct yetty_yplot_buffer_input));
            if (!bufs) {
                return NULL;
            }
            heap = 1;
        }
        for (size_t i = 0; i < self->data.yplot.buffer_count; i++) {
            bufs[i].samples = self->data.yplot.buffers[i].samples;
            bufs[i].count = self->data.yplot.buffers[i].count;
            bufs[i].color = self->data.yplot.buffers[i].color;
        }
        r = yetty_yplot_render_with_buffers(src, src_len, bufs, self->data.yplot.buffer_count,
                                            &cfg);
        if (heap) {
            free(bufs);
        }
    } else {
        r = yetty_yplot_render(src, src_len, &cfg);
    }
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    return r.value;
}

/*=============================================================================
 * Widget vtable
 *===========================================================================*/

static struct yetty_ycore_void_result yplot_render(struct yetty_ygui_widget *self,
                                                   struct yetty_ygui_render_ctx *ctx)
{
    float w = self->layout_w;
    float h = self->layout_h;
    ydebug(
        "yplot_render id=%s layout=(%.1f,%.1f %.1fx%.1f) parent=%s parent_layout=(%.1f,%.1f "
        "%.1fx%.1f) parent_content=(%.1f,%.1f %.1fx%.1f)",
        self->id ? self->id : "?", self->layout_x, self->layout_y, self->layout_w, self->layout_h,
        self->parent && self->parent->id ? self->parent->id : "?",
        self->parent ? self->parent->layout_x : 0.0f, self->parent ? self->parent->layout_y : 0.0f,
        self->parent ? self->parent->layout_w : 0.0f, self->parent ? self->parent->layout_h : 0.0f,
        self->parent ? self->parent->content_x : 0.0f,
        self->parent ? self->parent->content_y : 0.0f,
        self->parent ? self->parent->content_w : 0.0f,
        self->parent ? self->parent->content_h : 0.0f);
    if (!self->data.yplot.cached || self->data.yplot.last_w != w || self->data.yplot.last_h != h) {
        if (self->data.yplot.cached) {
            yetty_ydraw_draw_list_destroy(self->data.yplot.cached);
            self->data.yplot.cached = NULL;
        }
        self->data.yplot.cached = yplot_build_buffer(self, w, h);
        self->data.yplot.last_w = w;
        self->data.yplot.last_h = h;
    }
    if (!self->data.yplot.cached) {
        return YETTY_OK_VOID();
    }
    /* Figure-local emission: yplot is now its own YPLOT figure whose
     * rect carries the widget's absolute position. Prims inside the
     * figure are relative to that rect's origin, so dx=dy=0 — the
     * receiver-side ygrid render adds rect.min on draw. The cached
     * buffer is already authored in widget-local coords (bounds_x/y =
     * 0 inside yplot_build_buffer), so no translation is needed. */
    return yetty_ygui_internal_emit_buffer_translated(ctx, self->data.yplot.cached, 0.0f, 0.0f);
}

static void yplot_destroy(struct yetty_ygui_widget *self)
{
    yplot_invalidate_cache(self);
    free(self->data.yplot.source);
    self->data.yplot.source = NULL;
    yplot_free_buffers(self);
}

/* Custom render_all — yplot lives in its OWN figure kind (YPLOT), not
 * inside the surrounding chrome ygrid. The yplot's body bytes are the
 * cached prim stream; the figure rect is the widget rect. Receiver
 * still routes YPLOT through the ygrid factory today (same SDF/glyph
 * pipeline), but the wire stays semantically labelled. */
static struct yetty_ycore_void_result yplot_render_all(struct yetty_ygui_widget *self,
                                                       struct yetty_ygui_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        return YETTY_OK_VOID();
    }
    self->was_rendered = 1;
    uint32_t marker =
        yetty_ygui_widget_open_group_as_kind(self, ctx, YETTY_YFIGURE_KIND_YPLOT, yplot_render);
    yetty_ygui_widget_close_group(self, ctx, marker);
    return YETTY_OK_VOID();
}

static const struct yetty_ygui_widget_vtable *yplot_vtable_ptr(void)
{
    static const struct yetty_ygui_widget_vtable vt = {
        .render = yplot_render,
        .render_all = yplot_render_all,
        .destroy = yplot_destroy,
    };
    return &vt;
}

/*=============================================================================
 * Construction + setters
 *===========================================================================*/

static struct yetty_ygui_widget *yplot_alloc(struct yetty_ygui_engine *engine, const char *id,
                                             float x, float y, float w, float h)
{
    struct yetty_ygui_widget *widget =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_YPLOT, id);
    if (!widget) {
        return NULL;
    }
    yetty_ygui_widget_init_base(widget, x, y, w, h);
    widget->data.yplot.source = NULL;
    widget->data.yplot.source_len = 0;
    memset(&widget->data.yplot.cfg, 0, sizeof(widget->data.yplot.cfg));
    widget->data.yplot.has_cfg = 0;
    widget->data.yplot.buffers = NULL;
    widget->data.yplot.buffer_count = 0;
    widget->data.yplot.cached = NULL;
    widget->data.yplot.last_w = 0.0f;
    widget->data.yplot.last_h = 0.0f;
    widget->vtable = yplot_vtable_ptr();
    yetty_ygui_engine_attach_widget(engine, widget);
    return widget;
}

static int yplot_set_source_locked(struct yetty_ygui_widget *widget, const char *source,
                                   size_t source_len)
{
    if (!source) {
        free(widget->data.yplot.source);
        widget->data.yplot.source = NULL;
        widget->data.yplot.source_len = 0;
        return 1;
    }
    size_t len = source_len ? source_len : strlen(source);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        return 0;
    }
    memcpy(copy, source, len);
    copy[len] = '\0';
    free(widget->data.yplot.source);
    widget->data.yplot.source = copy;
    widget->data.yplot.source_len = len;
    return 1;
}

static int yplot_set_buffers_locked(struct yetty_ygui_widget *widget,
                                    const struct yetty_yplot_buffer_input *buffers,
                                    size_t buffer_count)
{
    yplot_free_buffers(widget);
    if (!buffers || buffer_count == 0) {
        return 1;
    }
    struct yplot_buffer_slot *slots =
        (struct yplot_buffer_slot *)calloc(buffer_count, sizeof(struct yplot_buffer_slot));
    if (!slots) {
        return 0;
    }
    for (size_t i = 0; i < buffer_count; i++) {
        slots[i].count = buffers[i].count;
        slots[i].color = buffers[i].color;
        if (buffers[i].samples && buffers[i].count > 0) {
            slots[i].samples = (float *)malloc(buffers[i].count * sizeof(float));
            if (!slots[i].samples) {
                for (size_t j = 0; j < i; j++) {
                    free(slots[j].samples);
                }
                free(slots);
                return 0;
            }
            memcpy(slots[i].samples, buffers[i].samples, buffers[i].count * sizeof(float));
        }
    }
    widget->data.yplot.buffers = slots;
    widget->data.yplot.buffer_count = buffer_count;
    return 1;
}

struct yetty_ygui_widget *yetty_ygui_engine_yplot_from_source(
    struct yetty_ygui_engine *engine, const char *id, float x, float y, float w, float h,
    const char *source, size_t source_len, const struct yetty_yplot_render_config *config)
{
    return yetty_ygui_engine_yplot_from_buffers(engine, id, x, y, w, h, source, source_len,
                                                /*buffers=*/NULL, /*buffer_count=*/0, config);
}

struct yetty_ygui_widget *yetty_ygui_engine_yplot_from_buffers(
    struct yetty_ygui_engine *engine, const char *id, float x, float y, float w, float h,
    const char *source, size_t source_len, const struct yetty_yplot_buffer_input *buffers,
    size_t buffer_count, const struct yetty_yplot_render_config *config)
{
    struct yetty_ygui_widget *widget = yplot_alloc(engine, id, x, y, w, h);
    if (!widget) {
        return NULL;
    }
    if (config) {
        widget->data.yplot.cfg = *config;
        widget->data.yplot.has_cfg = 1;
    }
    if (!yplot_set_source_locked(widget, source, source_len) ||
        !yplot_set_buffers_locked(widget, buffers, buffer_count)) {
        /* Allocation failure — leave widget empty (renders nothing). */
    }
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return widget;
}

struct yetty_ycore_void_result yetty_ygui_widget_yplot_set_source(
    struct yetty_ygui_widget *widget, const char *source, size_t source_len,
    const struct yetty_yplot_render_config *config)
{
    return yetty_ygui_widget_yplot_set_buffers(widget, source, source_len,
                                               /*buffers=*/NULL, /*buffer_count=*/0, config);
}

struct yetty_ycore_void_result yetty_ygui_widget_yplot_set_buffers(
    struct yetty_ygui_widget *widget, const char *source, size_t source_len,
    const struct yetty_yplot_buffer_input *buffers, size_t buffer_count,
    const struct yetty_yplot_render_config *config)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_YPLOT) {
        return YETTY_ERR(yetty_ycore_void, "yplot_set_buffers: not a yplot widget");
    }
    if (config) {
        widget->data.yplot.cfg = *config;
        widget->data.yplot.has_cfg = 1;
    } else {
        widget->data.yplot.has_cfg = 0;
    }
    if (!yplot_set_source_locked(widget, source, source_len)) {
        return YETTY_ERR(yetty_ycore_void, "yplot_set_buffers: source alloc failed");
    }
    if (!yplot_set_buffers_locked(widget, buffers, buffer_count)) {
        return YETTY_ERR(yetty_ycore_void, "yplot_set_buffers: buffers alloc failed");
    }
    yplot_invalidate_cache(widget);
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return YETTY_OK_VOID();
}
