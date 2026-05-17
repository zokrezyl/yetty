/*
 * ygui_yreadme.c — yreadme (Markdown) widget.
 *
 * Render path: yreadme → draw_list → RICH widget.
 *
 * The RICH widget owns the produced buffer and handles per-frame
 * translation by its resolved layout origin (see ygui_rich.c).
 */

#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_yreadme.h>
#include <yetty/ymarkdown/ymarkdown.h>
#include <yetty/yreadme/yreadme.h>

static struct yetty_ygui_widget *attach_to_rich(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    struct yetty_ymarkdown_render_result r)
{
    if (YETTY_IS_ERR(r)) {
        yetty_ycore_error_destroy(r.error);
        return NULL;
    }
    struct yetty_ygui_widget *widget = yetty_ygui_engine_rich(engine, id, x, y, w, h);
    if (!widget) {
        yetty_ydraw_draw_list_destroy(r.value.buffer);
        return NULL;
    }
    yetty_ygui_widget_rich_set_buffer(widget, r.value.buffer);
    return widget;
}

struct yetty_ygui_widget *yetty_ygui_engine_yreadme_from_file(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h, const char *path)
{
    struct yetty_ymarkdown_render_config cfg = yetty_yreadme_default_config(w, h);
    return attach_to_rich(engine, id, x, y, w, h,
                          yetty_yreadme_render_from_file(path, &cfg));
}

struct yetty_ygui_widget *yetty_ygui_engine_yreadme_from_buffer(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const uint8_t *data, size_t len)
{
    struct yetty_ymarkdown_render_config cfg = yetty_yreadme_default_config(w, h);
    return attach_to_rich(engine, id, x, y, w, h,
                          yetty_yreadme_render_from_buffer(data, len, &cfg));
}
