/*
 * ygui_yvideo.c — yvideo widget.
 *
 * Builds a yvideo complex prim (via yetty_yvideo_core) and hands it
 * to a RICH widget. The widget owns the buffer; set_* / clear rebuild
 * it and swap it in-place. Widget id is preserved across swaps so the
 * receiving scene-canvas folds the change into the same CMD_GROUP —
 * the previous yvideo entity is torn down (decoder, audio device,
 * animation subscription) and a fresh one is created with the new
 * bytes.
 *
 * MP4 ingestion is delegated to yvideo-mp4.h (yetty_yvideo_core).
 * The demuxer is single-sourced there; the widget just calls the
 * helper and attaches the resulting draw_list.
 */

#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yvideo.h>
#include <yetty/yvideo/yvideo.h>
#include <yetty/yvideo/yvideo-mp4.h>

#include <stdint.h>

/* H.264 in, draw_list out. The widget's painted size supplies the
 * default bounds when the caller didn't fix them in `config`. */
static struct yetty_ydraw_draw_list *build_h264_buffer(
    const uint8_t *nal_bytes, size_t nal_len,
    const struct yetty_yvideo_render_config *config,
    float widget_w, float widget_h)
{
    if (!config || config->video_w == 0u || config->video_h == 0u) {
        return NULL;
    }
    struct yetty_yvideo_render_config cfg = *config;
    if (cfg.bounds_w <= 0.0f) {
        cfg.bounds_w = widget_w > 0.0f ? widget_w : (float)config->video_w;
    }
    if (cfg.bounds_h <= 0.0f) {
        cfg.bounds_h = widget_h > 0.0f ? widget_h : (float)config->video_h;
    }
    if (cfg.fps <= 0.0f) cfg.fps = 30.0f;
    if (cfg.color_matrix == 0u) cfg.color_matrix = 1u; /* BT.709 */
    if (cfg.flags == 0u) cfg.flags = YETTY_YVIDEO_FLAG_LOOP | YETTY_YVIDEO_FLAG_AUTOPLAY;

    struct yetty_ydraw_draw_list_result r =
        yetty_yvideo_render(nal_bytes, nal_len, NULL, 0, &cfg);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    return r.value;
}

static struct yetty_ydraw_draw_list *build_mp4_buffer_from_bytes(
    const uint8_t *mp4_bytes, size_t mp4_len,
    const struct yetty_yvideo_render_config *overrides,
    float widget_w, float widget_h)
{
    struct yetty_yvideo_render_config cfg = {0};
    if (overrides) cfg = *overrides;
    if (cfg.bounds_w <= 0.0f) cfg.bounds_w = widget_w;
    if (cfg.bounds_h <= 0.0f) cfg.bounds_h = widget_h;

    struct yetty_ydraw_draw_list_result r =
        yetty_yvideo_render_from_mp4_bytes(mp4_bytes, mp4_len, &cfg);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    return r.value;
}

static struct yetty_ydraw_draw_list *build_mp4_buffer_from_file(
    const char *path,
    const struct yetty_yvideo_render_config *overrides,
    float widget_w, float widget_h)
{
    struct yetty_yvideo_render_config cfg = {0};
    if (overrides) cfg = *overrides;
    if (cfg.bounds_w <= 0.0f) cfg.bounds_w = widget_w;
    if (cfg.bounds_h <= 0.0f) cfg.bounds_h = widget_h;

    struct yetty_ydraw_draw_list_result r =
        yetty_yvideo_render_from_mp4_file(path, &cfg);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    return r.value;
}

static void widget_size(struct yetty_ygui_widget *widget, float *out_w, float *out_h)
{
    *out_w = 0.0f;
    *out_h = 0.0f;
    if (!widget) {
        return;
    }
    struct rectangle_result br = yetty_ygui_widget_get_content_box(widget);
    if (YETTY_IS_OK(br)) {
        float w = br.value.max.x - br.value.min.x;
        float h = br.value.max.y - br.value.min.y;
        if (w > 1.0f) *out_w = w;
        if (h > 1.0f) *out_h = h;
    } else {
        yetty_ycore_error_destroy(br.error);
    }
}

static struct yetty_ygui_widget *attach_to_rich(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    struct yetty_ydraw_draw_list *buf)
{
    if (!buf) {
        return NULL;
    }
    struct yetty_ygui_widget *widget = yetty_ygui_engine_rich(engine, id, x, y, w, h);
    if (!widget) {
        yetty_ydraw_draw_list_destroy(buf);
        return NULL;
    }
    yetty_ygui_widget_rich_set_buffer(widget, buf);
    return widget;
}

/*---------------------------------------------------------------------------
 * Public widget API.
 *-------------------------------------------------------------------------*/

struct yetty_ygui_widget *yetty_ygui_engine_yvideo_from_h264(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const uint8_t *nal_bytes, size_t nal_len,
    const struct yetty_yvideo_render_config *config)
{
    return attach_to_rich(engine, id, x, y, w, h,
                          build_h264_buffer(nal_bytes, nal_len, config, w, h));
}

struct yetty_ygui_widget *yetty_ygui_engine_yvideo_from_mp4_bytes(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const uint8_t *mp4_bytes, size_t mp4_len,
    const struct yetty_yvideo_render_config *config_overrides)
{
    return attach_to_rich(engine, id, x, y, w, h,
                          build_mp4_buffer_from_bytes(mp4_bytes, mp4_len,
                                                     config_overrides, w, h));
}

struct yetty_ygui_widget *yetty_ygui_engine_yvideo_from_mp4_file(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h, const char *path,
    const struct yetty_yvideo_render_config *config_overrides)
{
    return attach_to_rich(engine, id, x, y, w, h,
                          build_mp4_buffer_from_file(path, config_overrides, w, h));
}

struct yetty_ycore_void_result yetty_ygui_widget_yvideo_set_h264(
    struct yetty_ygui_widget *widget,
    const uint8_t *nal_bytes, size_t nal_len,
    const struct yetty_yvideo_render_config *config)
{
    if (!widget) {
        return YETTY_ERR(yetty_ycore_void, "ygui_yvideo_set_h264: widget is NULL");
    }
    float w = 0.0f, h = 0.0f;
    widget_size(widget, &w, &h);
    struct yetty_ydraw_draw_list *buf = build_h264_buffer(nal_bytes, nal_len, config, w, h);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygui_yvideo_set_h264: yvideo render failed");
    }
    yetty_ygui_widget_rich_set_buffer(widget, buf);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_widget_yvideo_set_mp4_bytes(
    struct yetty_ygui_widget *widget,
    const uint8_t *mp4_bytes, size_t mp4_len,
    const struct yetty_yvideo_render_config *config_overrides)
{
    if (!widget) {
        return YETTY_ERR(yetty_ycore_void, "ygui_yvideo_set_mp4_bytes: widget is NULL");
    }
    float w = 0.0f, h = 0.0f;
    widget_size(widget, &w, &h);
    struct yetty_ydraw_draw_list *buf =
        build_mp4_buffer_from_bytes(mp4_bytes, mp4_len, config_overrides, w, h);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygui_yvideo_set_mp4_bytes: mp4 demux / yvideo render failed");
    }
    yetty_ygui_widget_rich_set_buffer(widget, buf);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_widget_yvideo_set_mp4_file(
    struct yetty_ygui_widget *widget, const char *path,
    const struct yetty_yvideo_render_config *config_overrides)
{
    if (!widget) {
        return YETTY_ERR(yetty_ycore_void, "ygui_yvideo_set_mp4_file: widget is NULL");
    }
    float w = 0.0f, h = 0.0f;
    widget_size(widget, &w, &h);
    struct yetty_ydraw_draw_list *buf =
        build_mp4_buffer_from_file(path, config_overrides, w, h);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygui_yvideo_set_mp4_file: mp4 demux / yvideo render failed");
    }
    yetty_ygui_widget_rich_set_buffer(widget, buf);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_widget_yvideo_clear(struct yetty_ygui_widget *widget)
{
    if (!widget) {
        return YETTY_ERR(yetty_ycore_void, "ygui_yvideo_clear: widget is NULL");
    }
    struct yetty_ydraw_draw_list_config dlcfg = {0};
    struct yetty_ydraw_draw_list_result br = yetty_ydraw_draw_list_config_buffer_create(&dlcfg);
    if (YETTY_IS_ERR(br)) {
        return YETTY_ERR(yetty_ycore_void, "ygui_yvideo_clear: empty draw_list", br);
    }
    yetty_ygui_widget_rich_set_buffer(widget, br.value);
    return YETTY_OK_VOID();
}
