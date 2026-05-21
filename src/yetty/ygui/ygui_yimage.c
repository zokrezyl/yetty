/*
 * ygui_yimage.c — image widget.
 *
 * Render path: yimage (stb_image decode → yimage complex prim) →
 * draw_list → RICH widget. The RICH widget translates the yimage prim's
 * bounds by the widget's resolved layout origin (the translate_complex
 * branch in ygui_rich.c shifts the FAM bounds_x/bounds_y).
 *
 * Bounds_w / bounds_h are taken from the widget's authored (w, h) at
 * construction time, and from the widget's resolved content box on
 * set_file / set_buffer. Source pixels are sampled with hardware
 * bilinear at display resolution.
 */

#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yimage.h>
#include <yetty/yimage/yimage.h>

static struct yetty_ydraw_draw_list *build_buffer_from_path(
    const char *path, float bounds_w, float bounds_h)
{
    if (!path) {
        return NULL;
    }
    struct yetty_yimage_render_config cfg = {
        .bounds_x = 0.0f, .bounds_y = 0.0f,
        .bounds_w = bounds_w, .bounds_h = bounds_h,
    };
    struct yetty_ydraw_draw_list_result r = yetty_yimage_render_path(path, &cfg);
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    return r.value;
}

static struct yetty_ydraw_draw_list *build_buffer_from_bytes(
    const uint8_t *data, size_t len, float bounds_w, float bounds_h)
{
    if (!data || len == 0) {
        return NULL;
    }
    struct yetty_yimage_render_config cfg = {
        .bounds_x = 0.0f, .bounds_y = 0.0f,
        .bounds_w = bounds_w, .bounds_h = bounds_h,
    };
    struct yetty_ydraw_draw_list_result r = yetty_yimage_render(data, len, &cfg);
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

struct yetty_ygui_widget *yetty_ygui_engine_yimage_from_file(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h, const char *path)
{
    return attach_to_rich(engine, id, x, y, w, h,
                          build_buffer_from_path(path, w, h));
}

struct yetty_ygui_widget *yetty_ygui_engine_yimage_from_buffer(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const uint8_t *data, size_t len)
{
    return attach_to_rich(engine, id, x, y, w, h,
                          build_buffer_from_bytes(data, len, w, h));
}

struct yetty_ycore_void_result yetty_ygui_widget_yimage_set_file(
    struct yetty_ygui_widget *widget, const char *path)
{
    if (!widget) {
        return YETTY_ERR(yetty_ycore_void, "ygui_yimage_set_file: widget is NULL");
    }
    float w = 0.0f, h = 0.0f;
    widget_size(widget, &w, &h);
    struct yetty_ydraw_draw_list *buf = build_buffer_from_path(path, w, h);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygui_yimage_set_file: render failed");
    }
    yetty_ygui_widget_rich_set_buffer(widget, buf);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_widget_yimage_set_buffer(
    struct yetty_ygui_widget *widget, const uint8_t *data, size_t len)
{
    if (!widget) {
        return YETTY_ERR(yetty_ycore_void, "ygui_yimage_set_buffer: widget is NULL");
    }
    float w = 0.0f, h = 0.0f;
    widget_size(widget, &w, &h);
    struct yetty_ydraw_draw_list *buf = build_buffer_from_bytes(data, len, w, h);
    if (!buf) {
        return YETTY_ERR(yetty_ycore_void,
                         "ygui_yimage_set_buffer: render failed");
    }
    yetty_ygui_widget_rich_set_buffer(widget, buf);
    return YETTY_OK_VOID();
}
