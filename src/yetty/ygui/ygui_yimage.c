/*
 * ygui_yimage.c — dedicated image widget.
 *
 * Owns its source (file path OR in-memory bytes) and rebuilds the
 * yimage prim from the current resolved layout box on every render
 * where the box changed. No piggyback on rich — the widget IS its
 * own type, so layout / resize / set_file all converge on the same
 * render-time build path and the prim always fits the client area.
 */

#include "ygui_internal.h"
#include <yetty/yfigure/wire.h>

#include <stdlib.h>
#include <string.h>

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yimage.h>
#include <yetty/yimage/yimage.h>

/* Forward decl — same pattern ygui_rich.c uses. */
void yetty_ygui_engine_attach_widget(struct yetty_ygui_engine *engine,
                                     struct yetty_ygui_widget *widget);

/*=============================================================================
 * State management
 *===========================================================================*/

static void yimage_invalidate_cache(struct yetty_ygui_widget *self)
{
    if (self->data.yimage.cached) {
        yetty_ydraw_draw_list_destroy(self->data.yimage.cached);
        self->data.yimage.cached = NULL;
    }
    self->data.yimage.last_w = 0.0f;
    self->data.yimage.last_h = 0.0f;
}

static void yimage_clear_source(struct yetty_ygui_widget *self)
{
    free(self->data.yimage.path);
    self->data.yimage.path = NULL;
    free(self->data.yimage.data);
    self->data.yimage.data = NULL;
    self->data.yimage.data_len = 0;
}

/* Build a fresh yimage prim from the current source at (w, h). NULL
 * is returned for "no source", "decode failed", or "zero size". */
static struct yetty_ydraw_draw_list *yimage_build_buffer(struct yetty_ygui_widget *self,
                                                         float w, float h)
{
    if (w <= 0.0f || h <= 0.0f) {
        return NULL;
    }
    struct yetty_yimage_render_config cfg = {
        .bounds_x = 0.0f, .bounds_y = 0.0f,
        .bounds_w = w,    .bounds_h = h,
    };
    struct yetty_ydraw_draw_list_result r;
    if (self->data.yimage.path) {
        r = yetty_yimage_render_path(self->data.yimage.path, &cfg);
    } else if (self->data.yimage.data && self->data.yimage.data_len > 0) {
        r = yetty_yimage_render(self->data.yimage.data, self->data.yimage.data_len, &cfg);
    } else {
        return NULL;
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

static struct yetty_ycore_void_result yimage_render(struct yetty_ygui_widget *self,
                                                    struct yetty_ygui_render_ctx *ctx)
{
    /* Rebuild the prim if (a) we don't have one yet, or (b) the
     * resolved layout box has changed since the last build. Either
     * way the prim ends up sized to the current client area. */
    float w = self->layout_w;
    float h = self->layout_h;
    if (!self->data.yimage.cached ||
        self->data.yimage.last_w != w ||
        self->data.yimage.last_h != h) {
        if (self->data.yimage.cached) {
            yetty_ydraw_draw_list_destroy(self->data.yimage.cached);
            self->data.yimage.cached = NULL;
        }
        self->data.yimage.cached = yimage_build_buffer(self, w, h);
        self->data.yimage.last_w = w;
        self->data.yimage.last_h = h;
    }
    if (!self->data.yimage.cached) {
        return YETTY_OK_VOID();
    }
    /* Figure-local: yimage is its own YIMAGE figure whose rect carries
     * the widget's absolute position. Prims stay in widget-local coords
     * (the cached buffer was authored that way); the receiver adds
     * rect.min on draw. */
    return yetty_ygui_internal_emit_buffer_translated(
        ctx, self->data.yimage.cached, 0.0f, 0.0f);
}

static void yimage_destroy(struct yetty_ygui_widget *self)
{
    yimage_invalidate_cache(self);
    yimage_clear_source(self);
}

/* yimage lives in its own figure kind — sibling of any surrounding
 * widget's ygrid, not inlined into it. */
static struct yetty_ycore_void_result yimage_render_all(
    struct yetty_ygui_widget *self, struct yetty_ygui_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        return YETTY_OK_VOID();
    }
    self->was_rendered = 1;
    uint32_t marker = yetty_ygui_widget_open_group_as_kind(
        self, ctx, YETTY_YFIGURE_KIND_YIMAGE, yimage_render);
    yetty_ygui_widget_close_group(self, ctx, marker);
    return YETTY_OK_VOID();
}

static const struct yetty_ygui_widget_vtable *yimage_vtable_ptr(void)
{
    static const struct yetty_ygui_widget_vtable vt = {
        .render = yimage_render,
        .render_all = yimage_render_all,
        .destroy = yimage_destroy,
    };
    return &vt;
}

/*=============================================================================
 * Construction + setters
 *===========================================================================*/

static struct yetty_ygui_widget *yimage_alloc(struct yetty_ygui_engine *engine,
                                              const char *id,
                                              float x, float y, float w, float h)
{
    struct yetty_ygui_widget *widget =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_YIMAGE, id);
    if (!widget) {
        return NULL;
    }
    yetty_ygui_widget_init_base(widget, x, y, w, h);
    widget->data.yimage.path = NULL;
    widget->data.yimage.data = NULL;
    widget->data.yimage.data_len = 0;
    widget->data.yimage.cached = NULL;
    widget->data.yimage.last_w = 0.0f;
    widget->data.yimage.last_h = 0.0f;
    widget->vtable = yimage_vtable_ptr();
    yetty_ygui_engine_attach_widget(engine, widget);
    return widget;
}

static int yimage_set_path_locked(struct yetty_ygui_widget *widget, const char *path)
{
    if (!path) {
        yimage_clear_source(widget);
        return 1;
    }
    char *copy = (char *)malloc(strlen(path) + 1);
    if (!copy) {
        return 0;
    }
    memcpy(copy, path, strlen(path) + 1);
    yimage_clear_source(widget);
    widget->data.yimage.path = copy;
    return 1;
}

static int yimage_set_data_locked(struct yetty_ygui_widget *widget,
                                  const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        yimage_clear_source(widget);
        return 1;
    }
    uint8_t *copy = (uint8_t *)malloc(len);
    if (!copy) {
        return 0;
    }
    memcpy(copy, data, len);
    yimage_clear_source(widget);
    widget->data.yimage.data = copy;
    widget->data.yimage.data_len = len;
    return 1;
}

struct yetty_ygui_widget *yetty_ygui_engine_yimage_from_file(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h, const char *path)
{
    struct yetty_ygui_widget *widget = yimage_alloc(engine, id, x, y, w, h);
    if (!widget) {
        return NULL;
    }
    if (!yimage_set_path_locked(widget, path)) {
        /* Allocation failure on the path copy. The widget itself is
         * still valid + attached; leaving it empty is fine — render
         * paints nothing until set_file lands. */
    }
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return widget;
}

struct yetty_ygui_widget *yetty_ygui_engine_yimage_from_buffer(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const uint8_t *data, size_t len)
{
    struct yetty_ygui_widget *widget = yimage_alloc(engine, id, x, y, w, h);
    if (!widget) {
        return NULL;
    }
    if (!yimage_set_data_locked(widget, data, len)) {
        /* same rationale as above */
    }
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return widget;
}

struct yetty_ycore_void_result yetty_ygui_widget_yimage_set_file(
    struct yetty_ygui_widget *widget, const char *path)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_YIMAGE) {
        return YETTY_ERR(yetty_ycore_void, "yimage_set_file: not a yimage widget");
    }
    if (!yimage_set_path_locked(widget, path)) {
        return YETTY_ERR(yetty_ycore_void, "yimage_set_file: alloc failed");
    }
    yimage_invalidate_cache(widget);
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_widget_yimage_set_buffer(
    struct yetty_ygui_widget *widget, const uint8_t *data, size_t len)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_YIMAGE) {
        return YETTY_ERR(yetty_ycore_void, "yimage_set_buffer: not a yimage widget");
    }
    if (!yimage_set_data_locked(widget, data, len)) {
        return YETTY_ERR(yetty_ycore_void, "yimage_set_buffer: alloc failed");
    }
    yimage_invalidate_cache(widget);
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return YETTY_OK_VOID();
}
