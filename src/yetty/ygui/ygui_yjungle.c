/*
 * ygui_yjungle.c — dedicated yjungle widget.
 *
 * Owns its yjungle producer + delta/accumulator/flat buffers + a
 * mirror of the live segment map. yjungle is an INCREMENTAL producer:
 * each tick writes a delta stream (CMD_ZERO + CMD_DELETE / CMD_GROUP
 * records) into `delta`. The widget folds the delta into `live[]`,
 * rebuilds `acc` from `live[]`, and flattens into `flat` for paint.
 */

#include "ygui_internal.h"
#include "ygui_flatten.h"
#include <yetty/yfigure/wire.h>

#include <stdlib.h>
#include <string.h>

#include <yetty/ydraw-core/cmds.h>
#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yjungle.h>
#include <yetty/yjungle/yjungle.h>

void yetty_ygui_engine_attach_widget(struct yetty_ygui_engine *engine,
                                     struct yetty_ygui_widget *widget);

/*=============================================================================
 * Live segment map (mirrors what scene-canvas would build on the wire)
 *===========================================================================*/

static void live_clear(struct yetty_ygui_widget *self)
{
    for (size_t i = 0; i < self->data.yjungle.live_count; i++) {
        free(self->data.yjungle.live[i].bytes);
    }
    self->data.yjungle.live_count = 0;
}

static void live_remove(struct yetty_ygui_widget *self, uint32_t id)
{
    for (size_t i = 0; i < self->data.yjungle.live_count; i++) {
        if (self->data.yjungle.live[i].id == id) {
            free(self->data.yjungle.live[i].bytes);
            self->data.yjungle.live[i] = self->data.yjungle.live[self->data.yjungle.live_count - 1];
            self->data.yjungle.live_count--;
            return;
        }
    }
}

static int live_append(struct yetty_ygui_widget *self, uint32_t id, const uint8_t *src, size_t size)
{
    if (self->data.yjungle.live_count == self->data.yjungle.live_cap) {
        size_t nc = self->data.yjungle.live_cap ? self->data.yjungle.live_cap * 2 : 64;
        struct yjungle_live_seg *nl =
            (struct yjungle_live_seg *)realloc(self->data.yjungle.live, nc * sizeof(*nl));
        if (!nl) {
            return -1;
        }
        self->data.yjungle.live = nl;
        self->data.yjungle.live_cap = nc;
    }
    uint8_t *copy = (uint8_t *)malloc(size);
    if (!copy) {
        return -1;
    }
    memcpy(copy, src, size);
    size_t i = self->data.yjungle.live_count++;
    self->data.yjungle.live[i].id = id;
    self->data.yjungle.live[i].bytes = copy;
    self->data.yjungle.live[i].size = size;
    return 0;
}

/* Walk the producer's delta stream and mutate live[]. Only the
 * subset of records yjungle actually emits is handled. */
static void apply_delta(struct yetty_ygui_widget *self)
{
    const uint8_t *bytes = (const uint8_t *)yetty_ydraw_draw_list_data(self->data.yjungle.delta);
    size_t len = yetty_ydraw_draw_list_size(self->data.yjungle.delta);
    if (!bytes || len == 0) {
        return;
    }
    size_t off = 0;
    while (off + sizeof(uint32_t) <= len) {
        uint32_t type = *(const uint32_t *)(bytes + off);
        if (type == YETTY_YDRAW_CMD_ZERO) {
            live_clear(self);
            if (off + 8 > len) {
                break;
            }
            off += 8;
            continue;
        }
        if (type == YETTY_YDRAW_CMD_DELETE) {
            if (off + 12 > len) {
                break;
            }
            uint32_t id = ((const uint32_t *)(bytes + off))[1];
            live_remove(self, id);
            off += 12;
            continue;
        }
        if (type == YETTY_YDRAW_CMD_GROUP) {
            if (off + 12 > len) {
                break;
            }
            uint32_t id = ((const uint32_t *)(bytes + off))[1];
            uint32_t payload_size = ((const uint32_t *)(bytes + off))[2];
            size_t total = (size_t)12 + payload_size;
            if (off + total > len) {
                break;
            }
            if (live_append(self, id, bytes + off, total) != 0) {
                return;
            }
            off += total;
            continue;
        }
        break;
    }
}

/*=============================================================================
 * Producer + buffer lifecycle
 *===========================================================================*/

static void yj_drop_producer(struct yetty_ygui_widget *self)
{
    if (self->data.yjungle.producer) {
        yetty_yjungle_destroy(self->data.yjungle.producer);
        self->data.yjungle.producer = NULL;
    }
    if (self->data.yjungle.delta) {
        yetty_ydraw_draw_list_destroy(self->data.yjungle.delta);
        self->data.yjungle.delta = NULL;
    }
    if (self->data.yjungle.acc) {
        yetty_ydraw_draw_list_destroy(self->data.yjungle.acc);
        self->data.yjungle.acc = NULL;
    }
    if (self->data.yjungle.flat) {
        yetty_ydraw_draw_list_destroy(self->data.yjungle.flat);
        self->data.yjungle.flat = NULL;
    }
    live_clear(self);
    self->data.yjungle.last_w = 0.0f;
    self->data.yjungle.last_h = 0.0f;
}

static int yj_alloc_buffer(struct yetty_ydraw_draw_list **out, float w, float h)
{
    struct yetty_ydraw_draw_list_config bcfg = {
        .scene_min_x = 0.0f,
        .scene_min_y = 0.0f,
        .scene_max_x = w,
        .scene_max_y = h,
    };
    struct yetty_ydraw_draw_list_result r = yetty_ydraw_draw_list_config_buffer_create(&bcfg);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return 0;
    }
    if (*out) {
        yetty_ydraw_draw_list_destroy(*out);
    }
    *out = r.value;
    return 1;
}

static int yj_ensure_attached(struct yetty_ygui_widget *self, float w, float h)
{
    if (w <= 0.0f || h <= 0.0f) {
        return 0;
    }
    if (self->data.yjungle.producer && self->data.yjungle.last_w == w &&
        self->data.yjungle.last_h == h) {
        return 1;
    }
    if (self->data.yjungle.producer) {
        struct yetty_ycore_void_result rr =
            yetty_yjungle_set_scene_size(self->data.yjungle.producer, w, h);
        if (YETTY_IS_ERR(rr)) {
            yetty_ycore_error_destroy(rr.error);
            yj_drop_producer(self);
        }
    }
    if (!self->data.yjungle.producer) {
        struct yetty_yjungle_config cfg =
            self->data.yjungle.has_cfg ? self->data.yjungle.cfg : yetty_yjungle_config_default();
        cfg.scene_width = w;
        cfg.scene_height = h;
        struct yetty_yjungle_ptr_result jr = yetty_yjungle_create(&cfg, self->data.yjungle.seed);
        if (YETTY_IS_ERR(jr)) {
            yetty_ycore_error_destroy(jr.error);
            return 0;
        }
        self->data.yjungle.producer = jr.value;
        live_clear(self);
        self->data.yjungle.t0_ms = 0;
    }
    if (!yj_alloc_buffer(&self->data.yjungle.delta, w, h)) {
        return 0;
    }
    if (!yj_alloc_buffer(&self->data.yjungle.acc, w, h)) {
        return 0;
    }
    if (!yj_alloc_buffer(&self->data.yjungle.flat, w, h)) {
        return 0;
    }
    self->data.yjungle.last_w = w;
    self->data.yjungle.last_h = h;
    return 1;
}

static int yj_rebuild_flat(struct yetty_ygui_widget *self)
{
    if (!self->data.yjungle.acc || !self->data.yjungle.flat) {
        return 0;
    }
    yetty_ydraw_draw_list_clear(self->data.yjungle.acc);
    yetty_ydraw_draw_list_set_scene_bounds(self->data.yjungle.acc, 0.0f, 0.0f,
                                           self->data.yjungle.last_w, self->data.yjungle.last_h);
    for (size_t i = 0; i < self->data.yjungle.live_count; i++) {
        struct yetty_ydraw_id_result ar =
            yetty_ydraw_draw_list_add_prim(self->data.yjungle.acc, self->data.yjungle.live[i].bytes,
                                           self->data.yjungle.live[i].size);
        if (YETTY_IS_ERR(ar)) {
            yetty_ycore_error_destroy(ar.error);
            return 0;
        }
    }
    yetty_ydraw_draw_list_clear(self->data.yjungle.flat);
    yetty_ydraw_draw_list_set_scene_bounds(self->data.yjungle.flat, 0.0f, 0.0f,
                                           self->data.yjungle.last_w, self->data.yjungle.last_h);
    struct yetty_ycore_void_result fr =
        yetty_ygui_flatten_draw_list(self->data.yjungle.flat, self->data.yjungle.acc);
    if (YETTY_IS_ERR(fr)) {
        yetty_ycore_error_destroy(fr.error);
        return 0;
    }
    return 1;
}

/*=============================================================================
 * Widget vtable
 *===========================================================================*/

static struct yetty_ycore_void_result yj_render(struct yetty_ygui_widget *self,
                                                struct yetty_ygui_render_ctx *ctx)
{
    float w = self->layout_w;
    float h = self->layout_h;
    if (!yj_ensure_attached(self, w, h)) {
        return YETTY_OK_VOID();
    }
    if (self->data.yjungle.live_count == 0) {
        /* No tick has fired yet — pull an initial frame so the tab
         * isn't blank. yjungle's first tick always emits CMD_ZERO + the
         * initial chain regardless of clock value. */
        yetty_ydraw_draw_list_clear(self->data.yjungle.delta);
        struct yetty_ycore_void_result tr =
            yetty_yjungle_tick(self->data.yjungle.producer, self->data.yjungle.delta, 0);
        if (YETTY_IS_ERR(tr)) {
            yetty_ycore_error_destroy(tr.error);
        } else if (yetty_ydraw_draw_list_size(self->data.yjungle.delta) > 0u) {
            apply_delta(self);
            yj_rebuild_flat(self);
        }
    }
    if (!self->data.yjungle.flat) {
        return YETTY_OK_VOID();
    }
    /* Figure-local: see ygui_yplot.c for rationale. */
    return yetty_ygui_internal_emit_buffer_translated(ctx, self->data.yjungle.flat, 0.0f, 0.0f);
}

static void yj_destroy(struct yetty_ygui_widget *self)
{
    yj_drop_producer(self);
    free(self->data.yjungle.live);
    self->data.yjungle.live = NULL;
    self->data.yjungle.live_cap = 0;
}

static struct yetty_ycore_void_result yj_render_all(struct yetty_ygui_widget *self,
                                                    struct yetty_ygui_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        return YETTY_OK_VOID();
    }
    self->was_rendered = 1;
    uint32_t marker =
        yetty_ygui_widget_open_group_as_kind(self, ctx, YETTY_YFIGURE_KIND_YJUNGLE, yj_render);
    yetty_ygui_widget_close_group(self, ctx, marker);
    return YETTY_OK_VOID();
}

static const struct yetty_ygui_widget_vtable *yj_vtable_ptr(void)
{
    static const struct yetty_ygui_widget_vtable vt = {
        .render = yj_render,
        .render_all = yj_render_all,
        .destroy = yj_destroy,
    };
    return &vt;
}

/*=============================================================================
 * Construction + public API
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_yjungle(struct yetty_ygui_engine *engine,
                                                    const char *id, float x, float y, float w,
                                                    float h,
                                                    const struct yetty_yjungle_config *config,
                                                    uint32_t seed)
{
    struct yetty_ygui_widget *widget =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_YJUNGLE, id);
    if (!widget) {
        return NULL;
    }
    yetty_ygui_widget_init_base(widget, x, y, w, h);
    widget->data.yjungle.producer = NULL;
    widget->data.yjungle.delta = NULL;
    widget->data.yjungle.acc = NULL;
    widget->data.yjungle.flat = NULL;
    if (config) {
        widget->data.yjungle.cfg = *config;
        widget->data.yjungle.has_cfg = 1;
    } else {
        memset(&widget->data.yjungle.cfg, 0, sizeof(widget->data.yjungle.cfg));
        widget->data.yjungle.has_cfg = 0;
    }
    widget->data.yjungle.seed = seed;
    widget->data.yjungle.t0_ms = 0;
    widget->data.yjungle.live = NULL;
    widget->data.yjungle.live_count = 0;
    widget->data.yjungle.live_cap = 0;
    widget->data.yjungle.last_w = 0.0f;
    widget->data.yjungle.last_h = 0.0f;
    widget->vtable = yj_vtable_ptr();
    yetty_ygui_engine_attach_widget(engine, widget);
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return widget;
}

struct yetty_ycore_void_result yetty_ygui_widget_yjungle_tick(struct yetty_ygui_widget *widget,
                                                              uint64_t now_ms)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_YJUNGLE) {
        return YETTY_ERR(yetty_ycore_void, "yjungle_tick: not a yjungle widget");
    }
    float w = widget->layout_w;
    float h = widget->layout_h;
    if (!yj_ensure_attached(widget, w, h)) {
        return YETTY_OK_VOID();
    }
    yetty_ydraw_draw_list_clear(widget->data.yjungle.delta);
    struct yetty_ycore_void_result tr =
        yetty_yjungle_tick(widget->data.yjungle.producer, widget->data.yjungle.delta, now_ms);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "yjungle_tick: producer tick failed");
    if (yetty_ydraw_draw_list_size(widget->data.yjungle.delta) == 0u) {
        return YETTY_OK_VOID();
    }
    apply_delta(widget);
    if (!yj_rebuild_flat(widget)) {
        return YETTY_ERR(yetty_ycore_void, "yjungle_tick: rebuild_flat failed");
    }
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return YETTY_OK_VOID();
}
