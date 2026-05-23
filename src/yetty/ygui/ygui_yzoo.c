/*
 * ygui_yzoo.c — dedicated yzoo widget.
 *
 * Owns its yzoo producer + raw buffer (yzoo_render target) + flat
 * buffer (paint primitives ready for emit). Each tick rewrites both
 * buffers; the render hook emits the flat buffer translated by the
 * widget's resolved layout origin. The producer is rebuilt when the
 * layout box changes — yzoo carries scene_width/height inside the
 * instance and ydraw-core buffers carry scene bounds, so a resize
 * means dropping both and re-creating at the new size.
 */

#include "ygui_internal.h"
#include "ygui_flatten.h"
#include <yetty/yfigure/wire.h>

#include <stdlib.h>
#include <string.h>

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yzoo.h>
#include <yetty/yzoo/yzoo.h>

/* Forward decl — same pattern ygui_rich.c uses. */
void yetty_ygui_engine_attach_widget(struct yetty_ygui_engine *engine,
                                     struct yetty_ygui_widget *widget);

/*=============================================================================
 * State management
 *===========================================================================*/

static void yzoo_drop_producer(struct yetty_ygui_widget *self)
{
    if (self->data.yzoo.producer) {
        yetty_yzoo_destroy(self->data.yzoo.producer);
        self->data.yzoo.producer = NULL;
    }
    if (self->data.yzoo.raw) {
        yetty_ydraw_draw_list_destroy(self->data.yzoo.raw);
        self->data.yzoo.raw = NULL;
    }
    if (self->data.yzoo.flat) {
        yetty_ydraw_draw_list_destroy(self->data.yzoo.flat);
        self->data.yzoo.flat = NULL;
    }
    self->data.yzoo.last_w = 0.0f;
    self->data.yzoo.last_h = 0.0f;
}

static int yzoo_ensure_attached(struct yetty_ygui_widget *self, float w, float h)
{
    if (w <= 0.0f || h <= 0.0f) {
        return 0;
    }
    if (self->data.yzoo.producer &&
        self->data.yzoo.last_w == w &&
        self->data.yzoo.last_h == h) {
        return 1;
    }
    if (self->data.yzoo.producer) {
        struct yetty_ycore_void_result rr =
            yetty_yzoo_set_scene_size(self->data.yzoo.producer, w, h);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
            yzoo_drop_producer(self);
        }
    }
    if (!self->data.yzoo.producer) {
        struct yetty_yzoo_config cfg =
            self->data.yzoo.has_cfg ? self->data.yzoo.cfg : yetty_yzoo_config_default();
        cfg.scene_width = w;
        cfg.scene_height = h;
        struct yetty_yzoo_ptr_result zr = yetty_yzoo_create(&cfg, self->data.yzoo.seed);
        if (YETTY_IS_ERR(zr)) {
            yetty_ycore_error_destroy(zr.error);
            return 0;
        }
        self->data.yzoo.producer = zr.value;
    }

    struct yetty_ydraw_draw_list_config bcfg = {
        .scene_min_x = 0.0f, .scene_min_y = 0.0f,
        .scene_max_x = w,    .scene_max_y = h,
    };
    if (self->data.yzoo.raw) {
        yetty_ydraw_draw_list_destroy(self->data.yzoo.raw);
        self->data.yzoo.raw = NULL;
    }
    if (self->data.yzoo.flat) {
        yetty_ydraw_draw_list_destroy(self->data.yzoo.flat);
        self->data.yzoo.flat = NULL;
    }
    struct yetty_ydraw_draw_list_result rr =
        yetty_ydraw_draw_list_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(rr)) {
        yetty_ycore_error_destroy(rr.error);
        return 0;
    }
    self->data.yzoo.raw = rr.value;

    struct yetty_ydraw_draw_list_result fr =
        yetty_ydraw_draw_list_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
        return 0;
    }
    self->data.yzoo.flat = fr.value;

    self->data.yzoo.last_w = w;
    self->data.yzoo.last_h = h;
    return 1;
}

static int yzoo_run_one_frame(struct yetty_ygui_widget *self)
{
    if (!self->data.yzoo.producer || !self->data.yzoo.raw || !self->data.yzoo.flat) {
        return 0;
    }
    struct yetty_ycore_void_result rr =
        yetty_yzoo_render(self->data.yzoo.producer, self->data.yzoo.raw,
                          self->data.yzoo.t_seconds);
    if (YETTY_IS_ERR(rr)) {
        yetty_ycore_error_destroy(rr.error);
        return 0;
    }
    yetty_ydraw_draw_list_clear(self->data.yzoo.flat);
    yetty_ydraw_draw_list_set_scene_bounds(self->data.yzoo.flat,
                                           0.0f, 0.0f,
                                           self->data.yzoo.last_w,
                                           self->data.yzoo.last_h);
    struct yetty_ycore_void_result fl =
        yetty_ygui_flatten_draw_list(self->data.yzoo.flat, self->data.yzoo.raw);
    if (YETTY_IS_ERR(fl)) {
        yetty_ycore_error_destroy(fl.error);
        return 0;
    }
    return 1;
}

/*=============================================================================
 * Widget vtable
 *===========================================================================*/

static struct yetty_ycore_void_result yzoo_render(struct yetty_ygui_widget *self,
                                                  struct yetty_ygui_render_ctx *ctx)
{
    float w = self->layout_w;
    float h = self->layout_h;
    if (!yzoo_ensure_attached(self, w, h)) {
        return YETTY_OK_VOID();
    }
    if (!self->data.yzoo.flat ||
        yetty_ydraw_draw_list_size(self->data.yzoo.flat) == 0u) {
        /* No frame has been rendered yet — paint an initial frame so
         * the tab isn't blank until the first tick fires. */
        yzoo_run_one_frame(self);
    }
    if (!self->data.yzoo.flat) {
        return YETTY_OK_VOID();
    }
    /* Figure-local: see ygui_yplot.c for rationale. */
    return yetty_ygui_internal_emit_buffer_translated(
        ctx, self->data.yzoo.flat, 0.0f, 0.0f);
}

static void yzoo_destroy(struct yetty_ygui_widget *self)
{
    yzoo_drop_producer(self);
}

static struct yetty_ycore_void_result yzoo_render_all(
    struct yetty_ygui_widget *self, struct yetty_ygui_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        return YETTY_OK_VOID();
    }
    self->was_rendered = 1;
    uint32_t marker = yetty_ygui_widget_open_group_as_kind(
        self, ctx, YETTY_YFIGURE_KIND_YZOO, yzoo_render);
    yetty_ygui_widget_close_group(self, ctx, marker);
    return YETTY_OK_VOID();
}

static const struct yetty_ygui_widget_vtable *yzoo_vtable_ptr(void)
{
    static const struct yetty_ygui_widget_vtable vt = {
        .render = yzoo_render,
        .render_all = yzoo_render_all,
        .destroy = yzoo_destroy,
    };
    return &vt;
}

/*=============================================================================
 * Construction + public API
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_yzoo(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const struct yetty_yzoo_config *config, uint32_t seed)
{
    struct yetty_ygui_widget *widget =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_YZOO, id);
    if (!widget) {
        return NULL;
    }
    yetty_ygui_widget_init_base(widget, x, y, w, h);
    widget->data.yzoo.producer = NULL;
    widget->data.yzoo.raw = NULL;
    widget->data.yzoo.flat = NULL;
    if (config) {
        widget->data.yzoo.cfg = *config;
        widget->data.yzoo.has_cfg = 1;
    } else {
        memset(&widget->data.yzoo.cfg, 0, sizeof(widget->data.yzoo.cfg));
        widget->data.yzoo.has_cfg = 0;
    }
    widget->data.yzoo.seed = seed;
    widget->data.yzoo.t_seconds = 0.0f;
    widget->data.yzoo.last_w = 0.0f;
    widget->data.yzoo.last_h = 0.0f;
    widget->vtable = yzoo_vtable_ptr();
    yetty_ygui_engine_attach_widget(engine, widget);
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return widget;
}

struct yetty_ycore_void_result yetty_ygui_widget_yzoo_tick(
    struct yetty_ygui_widget *widget, float dt_seconds)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_YZOO) {
        return YETTY_ERR(yetty_ycore_void, "yzoo_tick: not a yzoo widget");
    }
    float w = widget->layout_w;
    float h = widget->layout_h;
    if (!yzoo_ensure_attached(widget, w, h)) {
        /* No usable layout yet — skip; the first render will retry. */
        return YETTY_OK_VOID();
    }
    if (dt_seconds < 0.0f) dt_seconds = 0.0f;
    widget->data.yzoo.t_seconds += dt_seconds;
    if (!yzoo_run_one_frame(widget)) {
        return YETTY_ERR(yetty_ycore_void, "yzoo_tick: yzoo_render failed");
    }
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return YETTY_OK_VOID();
}
