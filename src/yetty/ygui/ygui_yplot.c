/*
 * ygui_yplot.c — yplot widget.
 *
 * Builds a yplot complex prim (via yetty_yplot_core) and hands it to
 * a RICH widget. The widget owns the buffer; set_source / set_buffers
 * builds a fresh one and swaps it in. Widget id stays stable across
 * swaps so the receiving scene-canvas folds the change into the
 * existing entity rather than recreating it.
 */

#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yplot.h>
#include <yetty/yplot/yplot.h>

/* Fall-back default size — matches the documented yplot defaults
 * (yplot.h "400 — width in pixels" / "200 — height in pixels"). Used
 * when neither the caller's config nor a resolved layout box give us
 * usable bounds. */
#define YGUI_YPLOT_DEFAULT_W 400.0f
#define YGUI_YPLOT_DEFAULT_H 200.0f

/* Build a fresh draw_list holding ONE yplot prim. Caller takes
 * ownership. NULL on parse / serialize / OOM failure. */
static struct yetty_ydraw_draw_list *build_buffer(
    float widget_w, float widget_h,
    const char *source, size_t source_len,
    const struct yetty_yplot_buffer_input *buffers, size_t buffer_count,
    const struct yetty_yplot_render_config *config)
{
    struct yetty_yplot_render_config cfg = {0};
    if (config) {
        cfg = *config;
    }
    /* Default bounds to the widget's authored / resolved size when
     * the caller didn't override them in the config. */
    if (cfg.bounds_w <= 0.0f) {
        cfg.bounds_w = widget_w > 0.0f ? widget_w : YGUI_YPLOT_DEFAULT_W;
    }
    if (cfg.bounds_h <= 0.0f) {
        cfg.bounds_h = widget_h > 0.0f ? widget_h : YGUI_YPLOT_DEFAULT_H;
    }
    /* yetty_yplot_render treats NULL source as an empty string; we
     * normalise so the buffer-only mode (source_len = 0) still works. */
    const char *src = source ? source : "";
    size_t src_len = source ? source_len : 0u;

    struct yetty_ydraw_draw_list_result r;
    if (buffers && buffer_count > 0) {
        r = yetty_yplot_render_with_buffers(src, src_len, buffers, buffer_count, &cfg);
    } else {
        r = yetty_yplot_render(src, src_len, &cfg);
    }
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    return r.value;
}

/* Resolve the widget's content size for default bounds. Falls back
 * to the yplot defaults when layout hasn't run yet (e.g. the widget
 * was just created and the engine hasn't computed boxes). */
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

struct yetty_ygui_widget *yetty_ygui_engine_yplot_from_source(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const char *source, size_t source_len,
    const struct yetty_yplot_render_config *config)
{
    return yetty_ygui_engine_yplot_from_buffers(engine, id, x, y, w, h,
                                                source, source_len,
                                                /*buffers=*/NULL, /*buffer_count=*/0, config);
}

struct yetty_ygui_widget *yetty_ygui_engine_yplot_from_buffers(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const char *source, size_t source_len,
    const struct yetty_yplot_buffer_input *buffers, size_t buffer_count,
    const struct yetty_yplot_render_config *config)
{
    struct yetty_ydraw_draw_list *buf =
        build_buffer(w, h, source, source_len, buffers, buffer_count, config);
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

struct yetty_ycore_void_result yetty_ygui_widget_yplot_set_source(
    struct yetty_ygui_widget *widget,
    const char *source, size_t source_len,
    const struct yetty_yplot_render_config *config)
{
    return yetty_ygui_widget_yplot_set_buffers(widget, source, source_len,
                                               /*buffers=*/NULL, /*buffer_count=*/0, config);
}

struct yetty_ycore_void_result yetty_ygui_widget_yplot_set_buffers(
    struct yetty_ygui_widget *widget,
    const char *source, size_t source_len,
    const struct yetty_yplot_buffer_input *buffers, size_t buffer_count,
    const struct yetty_yplot_render_config *config)
{
    if (!widget) {
        return YETTY_ERR(yetty_ycore_void, "ygui_yplot_set_buffers: widget is NULL");
    }
    float w = 0.0f, h = 0.0f;
    widget_size(widget, &w, &h);
    struct yetty_ydraw_draw_list *buf =
        build_buffer(w, h, source, source_len, buffers, buffer_count, config);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygui_yplot_set_buffers: yplot render failed");
    }
    yetty_ygui_widget_rich_set_buffer(widget, buf);
    return YETTY_OK_VOID();
}
