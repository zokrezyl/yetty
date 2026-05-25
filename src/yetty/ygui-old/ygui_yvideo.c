/*
 * ygui_yvideo.c — dedicated yvideo widget.
 *
 * Owns its source (raw H.264 NAL bytes OR MP4 container bytes OR a
 * file path) + render config; rebuilds the yvideo prim from the
 * current resolved layout box on every render where the box changed.
 * Same pattern as ygui_yimage / ygui_yplot — no piggyback on rich.
 *
 * MP4 demuxing is delegated to yvideo-mp4.h (yetty_yvideo_core) so the
 * widget never touches minimp4 itself.
 */

#include "ygui_internal.h"
#include <yetty/yfigure/wire.h>

#include <stdlib.h>
#include <string.h>

#include <yetty/ydraw-core/draw-list.h>
#include <yetty/ygui-old/ygui.h>
#include <yetty/ygui-old/ygui_yvideo.h>
#include <yetty/yvideo/yvideo.h>
#include <yetty/yvideo/yvideo-mp4.h>

/* Forward decl — same pattern ygui_rich.c uses. */
void yetty_ygui_old_engine_attach_widget(struct yetty_ygui_old_engine *engine,
                                     struct yetty_ygui_old_widget *widget);

enum yvideo_source_kind {
    YVIDEO_SRC_NONE = 0,
    YVIDEO_SRC_H264,
    YVIDEO_SRC_MP4_BYTES,
    YVIDEO_SRC_MP4_FILE,
};

/*=============================================================================
 * State management
 *===========================================================================*/

static void yvideo_invalidate_cache(struct yetty_ygui_old_widget *self)
{
    if (self->data.yvideo.cached) {
        yetty_ydraw_draw_list_destroy(self->data.yvideo.cached);
        self->data.yvideo.cached = NULL;
    }
    self->data.yvideo.last_w = 0.0f;
    self->data.yvideo.last_h = 0.0f;
}

static void yvideo_clear_source(struct yetty_ygui_old_widget *self)
{
    free(self->data.yvideo.nal_bytes);
    self->data.yvideo.nal_bytes = NULL;
    self->data.yvideo.nal_len = 0;
}

/* The yvideo widget's data union arm carries only `nal_bytes/nal_len`
 * + cfg. We piggyback an extra "kind tag + path" pair right here in
 * static helpers — the source slot ends up being one of:
 *   - raw H.264 NAL stream (nal_bytes + nal_len + cfg.video_w/h)
 *   - MP4 container bytes (nal_bytes + nal_len, kind=MP4_BYTES)
 *   - file path (path string stashed in nal_bytes as a NUL-terminated
 *     byte buffer, kind=MP4_FILE)
 * The discriminator is `cfg.flags`'s high byte (we mask it back out
 * before passing cfg to yvideo_render). Cleaner would be an explicit
 * field, but the data union is shared and adding a tag here keeps the
 * blast radius local. */

#define YVIDEO_SRC_TAG_SHIFT 24
#define YVIDEO_SRC_TAG_MASK (0xffu << YVIDEO_SRC_TAG_SHIFT)

static enum yvideo_source_kind yvideo_get_kind(const struct yetty_ygui_old_widget *self)
{
    return (enum yvideo_source_kind)((self->data.yvideo.cfg.flags >> YVIDEO_SRC_TAG_SHIFT) & 0xffu);
}

static void yvideo_set_kind(struct yetty_ygui_old_widget *self, enum yvideo_source_kind kind)
{
    self->data.yvideo.cfg.flags = (self->data.yvideo.cfg.flags & ~YVIDEO_SRC_TAG_MASK) |
                                  (((uint32_t)kind & 0xffu) << YVIDEO_SRC_TAG_SHIFT);
}

static uint32_t yvideo_cfg_flags_clean(const struct yetty_ygui_old_widget *self)
{
    return self->data.yvideo.cfg.flags & ~YVIDEO_SRC_TAG_MASK;
}

static struct yetty_ydraw_draw_list *yvideo_build_buffer(struct yetty_ygui_old_widget *self, float w,
                                                         float h)
{
    if (w <= 0.0f || h <= 0.0f) {
        return NULL;
    }
    enum yvideo_source_kind kind = yvideo_get_kind(self);
    if (kind == YVIDEO_SRC_NONE) {
        return NULL;
    }
    if (!self->data.yvideo.nal_bytes || self->data.yvideo.nal_len == 0) {
        return NULL;
    }

    struct yetty_yvideo_render_config cfg = self->data.yvideo.cfg;
    cfg.flags = yvideo_cfg_flags_clean(self);
    cfg.bounds_x = 0.0f;
    cfg.bounds_y = 0.0f;
    cfg.bounds_w = w;
    cfg.bounds_h = h;
    if (cfg.fps <= 0.0f) {
        cfg.fps = 30.0f;
    }
    if (cfg.color_matrix == 0u) {
        cfg.color_matrix = 1u; /* BT.709 */
    }
    if (cfg.flags == 0u) {
        cfg.flags = YETTY_YVIDEO_FLAG_LOOP | YETTY_YVIDEO_FLAG_AUTOPLAY;
    }

    struct yetty_ydraw_draw_list_result r;
    switch (kind) {
    case YVIDEO_SRC_H264:
        r = yetty_yvideo_render(self->data.yvideo.nal_bytes, self->data.yvideo.nal_len, NULL, 0,
                                &cfg);
        break;
    case YVIDEO_SRC_MP4_BYTES:
        r = yetty_yvideo_render_from_mp4_bytes(self->data.yvideo.nal_bytes,
                                               self->data.yvideo.nal_len, &cfg);
        break;
    case YVIDEO_SRC_MP4_FILE:
        /* nal_bytes here is a NUL-terminated path string. */
        r = yetty_yvideo_render_from_mp4_file((const char *)self->data.yvideo.nal_bytes, &cfg);
        break;
    default:
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

static struct yetty_ycore_void_result yvideo_render(struct yetty_ygui_old_widget *self,
                                                    struct yetty_ygui_old_render_ctx *ctx)
{
    float w = self->layout_w;
    float h = self->layout_h;
    if (!self->data.yvideo.cached || self->data.yvideo.last_w != w ||
        self->data.yvideo.last_h != h) {
        if (self->data.yvideo.cached) {
            yetty_ydraw_draw_list_destroy(self->data.yvideo.cached);
            self->data.yvideo.cached = NULL;
        }
        self->data.yvideo.cached = yvideo_build_buffer(self, w, h);
        self->data.yvideo.last_w = w;
        self->data.yvideo.last_h = h;
    }
    if (!self->data.yvideo.cached) {
        return YETTY_OK_VOID();
    }
    /* Figure-local: see ygui_yplot.c for rationale. */
    return yetty_ygui_old_internal_emit_buffer_translated(ctx, self->data.yvideo.cached, 0.0f, 0.0f);
}

static void yvideo_destroy(struct yetty_ygui_old_widget *self)
{
    yvideo_invalidate_cache(self);
    yvideo_clear_source(self);
}

static struct yetty_ycore_void_result yvideo_render_all(struct yetty_ygui_old_widget *self,
                                                        struct yetty_ygui_old_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_OLD_FLAG_VISIBLE)) {
        return YETTY_OK_VOID();
    }
    self->was_rendered = 1;
    struct yetty_ygui_old_group_marker_result mr =
        yetty_ygui_old_widget_open_group_as_kind(self, ctx, YETTY_YFIGURE_KIND_YVIDEO, yvideo_render);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, mr, "yvideo_render_all: open_group");
    struct yetty_ycore_void_result cr = yetty_ygui_old_widget_close_group(self, ctx, mr.value);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, cr, "yvideo_render_all: close_group");
    return YETTY_OK_VOID();
}

static const struct yetty_ygui_old_widget_vtable *yvideo_vtable_ptr(void)
{
    static const struct yetty_ygui_old_widget_vtable vt = {
        .render = yvideo_render,
        .render_all = yvideo_render_all,
        .destroy = yvideo_destroy,
    };
    return &vt;
}

/*=============================================================================
 * Construction + setters
 *===========================================================================*/

static struct yetty_ygui_old_widget *yvideo_alloc(struct yetty_ygui_old_engine *engine, const char *id,
                                              float x, float y, float w, float h)
{
    struct yetty_ygui_old_widget *widget =
        yetty_ygui_old_engine_widget_alloc(engine, YETTY_YGUI_OLD_WIDGET_YVIDEO, id);
    if (!widget) {
        return NULL;
    }
    yetty_ygui_old_widget_init_base(widget, x, y, w, h);
    widget->data.yvideo.nal_bytes = NULL;
    widget->data.yvideo.nal_len = 0;
    memset(&widget->data.yvideo.cfg, 0, sizeof(widget->data.yvideo.cfg));
    widget->data.yvideo.has_cfg = 0;
    widget->data.yvideo.cached = NULL;
    widget->data.yvideo.last_w = 0.0f;
    widget->data.yvideo.last_h = 0.0f;
    widget->vtable = yvideo_vtable_ptr();
    yetty_ygui_old_engine_attach_widget(engine, widget);
    return widget;
}

static int yvideo_set_bytes_locked(struct yetty_ygui_old_widget *widget, const uint8_t *bytes,
                                   size_t len, enum yvideo_source_kind kind)
{
    if (!bytes || len == 0) {
        yvideo_clear_source(widget);
        yvideo_set_kind(widget, YVIDEO_SRC_NONE);
        return 1;
    }
    uint8_t *copy = (uint8_t *)malloc(len);
    if (!copy) {
        return 0;
    }
    memcpy(copy, bytes, len);
    free(widget->data.yvideo.nal_bytes);
    widget->data.yvideo.nal_bytes = copy;
    widget->data.yvideo.nal_len = len;
    yvideo_set_kind(widget, kind);
    return 1;
}

static int yvideo_set_path_locked(struct yetty_ygui_old_widget *widget, const char *path)
{
    if (!path) {
        yvideo_clear_source(widget);
        yvideo_set_kind(widget, YVIDEO_SRC_NONE);
        return 1;
    }
    size_t len = strlen(path);
    uint8_t *copy = (uint8_t *)malloc(len + 1);
    if (!copy) {
        return 0;
    }
    memcpy(copy, path, len + 1);
    free(widget->data.yvideo.nal_bytes);
    widget->data.yvideo.nal_bytes = copy;
    widget->data.yvideo.nal_len = len + 1;
    yvideo_set_kind(widget, YVIDEO_SRC_MP4_FILE);
    return 1;
}

static void yvideo_apply_cfg(struct yetty_ygui_old_widget *widget,
                             const struct yetty_yvideo_render_config *config)
{
    enum yvideo_source_kind kind = yvideo_get_kind(widget);
    if (config) {
        widget->data.yvideo.cfg = *config;
        widget->data.yvideo.has_cfg = 1;
    } else {
        memset(&widget->data.yvideo.cfg, 0, sizeof(widget->data.yvideo.cfg));
        widget->data.yvideo.has_cfg = 0;
    }
    yvideo_set_kind(widget, kind);
}

/*---------------------------------------------------------------------------
 * Public widget API.
 *-------------------------------------------------------------------------*/

struct yetty_ygui_old_widget *yetty_ygui_old_engine_yvideo_from_h264(
    struct yetty_ygui_old_engine *engine, const char *id, float x, float y, float w, float h,
    const uint8_t *nal_bytes, size_t nal_len, const struct yetty_yvideo_render_config *config)
{
    if (!config || config->video_w == 0u || config->video_h == 0u) {
        return NULL;
    }
    struct yetty_ygui_old_widget *widget = yvideo_alloc(engine, id, x, y, w, h);
    if (!widget) {
        return NULL;
    }
    yvideo_apply_cfg(widget, config);
    if (!yvideo_set_bytes_locked(widget, nal_bytes, nal_len, YVIDEO_SRC_H264)) {
        /* Allocation failure — widget stays empty (renders nothing). */
    }
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return widget;
}

struct yetty_ygui_old_widget *yetty_ygui_old_engine_yvideo_from_mp4_bytes(
    struct yetty_ygui_old_engine *engine, const char *id, float x, float y, float w, float h,
    const uint8_t *mp4_bytes, size_t mp4_len,
    const struct yetty_yvideo_render_config *config_overrides)
{
    struct yetty_ygui_old_widget *widget = yvideo_alloc(engine, id, x, y, w, h);
    if (!widget) {
        return NULL;
    }
    yvideo_apply_cfg(widget, config_overrides);
    if (!yvideo_set_bytes_locked(widget, mp4_bytes, mp4_len, YVIDEO_SRC_MP4_BYTES)) {
        /* same rationale */
    }
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return widget;
}

struct yetty_ygui_old_widget *yetty_ygui_old_engine_yvideo_from_mp4_file(
    struct yetty_ygui_old_engine *engine, const char *id, float x, float y, float w, float h,
    const char *path, const struct yetty_yvideo_render_config *config_overrides)
{
    struct yetty_ygui_old_widget *widget = yvideo_alloc(engine, id, x, y, w, h);
    if (!widget) {
        return NULL;
    }
    yvideo_apply_cfg(widget, config_overrides);
    if (!yvideo_set_path_locked(widget, path)) {
        /* same rationale */
    }
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return widget;
}

struct yetty_ycore_void_result yetty_ygui_old_widget_yvideo_set_h264(
    struct yetty_ygui_old_widget *widget, const uint8_t *nal_bytes, size_t nal_len,
    const struct yetty_yvideo_render_config *config)
{
    if (!widget || widget->type != YETTY_YGUI_OLD_WIDGET_YVIDEO) {
        return YETTY_ERR(yetty_ycore_void, "yvideo_set_h264: not a yvideo widget");
    }
    if (!config || config->video_w == 0u || config->video_h == 0u) {
        return YETTY_ERR(yetty_ycore_void, "yvideo_set_h264: config requires video_w / video_h");
    }
    yvideo_apply_cfg(widget, config);
    if (!yvideo_set_bytes_locked(widget, nal_bytes, nal_len, YVIDEO_SRC_H264)) {
        return YETTY_ERR(yetty_ycore_void, "yvideo_set_h264: alloc failed");
    }
    yvideo_invalidate_cache(widget);
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_old_widget_yvideo_set_mp4_bytes(
    struct yetty_ygui_old_widget *widget, const uint8_t *mp4_bytes, size_t mp4_len,
    const struct yetty_yvideo_render_config *config_overrides)
{
    if (!widget || widget->type != YETTY_YGUI_OLD_WIDGET_YVIDEO) {
        return YETTY_ERR(yetty_ycore_void, "yvideo_set_mp4_bytes: not a yvideo widget");
    }
    yvideo_apply_cfg(widget, config_overrides);
    if (!yvideo_set_bytes_locked(widget, mp4_bytes, mp4_len, YVIDEO_SRC_MP4_BYTES)) {
        return YETTY_ERR(yetty_ycore_void, "yvideo_set_mp4_bytes: alloc failed");
    }
    yvideo_invalidate_cache(widget);
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_old_widget_yvideo_set_mp4_file(
    struct yetty_ygui_old_widget *widget, const char *path,
    const struct yetty_yvideo_render_config *config_overrides)
{
    if (!widget || widget->type != YETTY_YGUI_OLD_WIDGET_YVIDEO) {
        return YETTY_ERR(yetty_ycore_void, "yvideo_set_mp4_file: not a yvideo widget");
    }
    yvideo_apply_cfg(widget, config_overrides);
    if (!yvideo_set_path_locked(widget, path)) {
        return YETTY_ERR(yetty_ycore_void, "yvideo_set_mp4_file: alloc failed");
    }
    yvideo_invalidate_cache(widget);
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_old_widget_yvideo_clear(struct yetty_ygui_old_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_OLD_WIDGET_YVIDEO) {
        return YETTY_ERR(yetty_ycore_void, "yvideo_clear: not a yvideo widget");
    }
    yvideo_clear_source(widget);
    yvideo_set_kind(widget, YVIDEO_SRC_NONE);
    yvideo_invalidate_cache(widget);
    if (widget->engine) {
        widget->dirty = 1;
        widget->engine->dirty = 1;
    }
    return YETTY_OK_VOID();
}
