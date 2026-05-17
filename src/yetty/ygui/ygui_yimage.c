/*
 * ygui_yimage.c — image widget.
 *
 * Render path: yimage (stb_image decode → yimage complex prim) →
 * draw_list → RICH widget. The RICH widget translates the yimage prim's
 * bounds by the widget's resolved layout origin (the translate_complex
 * branch in ygui_rich.c shifts the FAM bounds_x/bounds_y).
 *
 * Bounds_w / bounds_h are taken from the widget's authored (w, h) so
 * the painted image fills the widget box. Source pixels are sampled
 * with hardware bilinear at display resolution.
 */

#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yimage.h>
#include <yetty/yimage/yimage.h>

static struct yetty_ygui_widget *attach_to_rich(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    struct yetty_ydraw_draw_list_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    struct yetty_ygui_widget *widget = yetty_ygui_engine_rich(engine, id, x, y, w, h);
    if (!widget) {
        yetty_ydraw_draw_list_destroy(r.value);
        return NULL;
    }
    yetty_ygui_widget_rich_set_buffer(widget, r.value);
    return widget;
}

struct yetty_ygui_widget *yetty_ygui_engine_yimage_from_file(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h, const char *path)
{
    if (!path) {
        return NULL;
    }
    struct yetty_yimage_render_config cfg = {
        .bounds_x = 0.0f, .bounds_y = 0.0f,
        .bounds_w = w,    .bounds_h = h,
    };
    return attach_to_rich(engine, id, x, y, w, h,
                          yetty_yimage_render_path(path, &cfg));
}

struct yetty_ygui_widget *yetty_ygui_engine_yimage_from_buffer(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return NULL;
    }
    struct yetty_yimage_render_config cfg = {
        .bounds_x = 0.0f, .bounds_y = 0.0f,
        .bounds_w = w,    .bounds_h = h,
    };
    return attach_to_rich(engine, id, x, y, w, h,
                          yetty_yimage_render(data, len, &cfg));
}
