/*
 * ygui_ymarkdown.c — Markdown widget.
 *
 * Render path: ymarkdown → draw_list → RICH widget.
 *
 * The RICH widget owns the produced buffer and handles per-frame
 * translation by its resolved layout origin (see ygui_rich.c).
 */

#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_ymarkdown.h>
#include <yetty/ymarkdown/ymarkdown.h>

#include <stdio.h>
#include <stdlib.h>

static char *slurp_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    size_t cap = 4096, len = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    for (;;) {
        if (len == cap) {
            size_t nc = cap * 2;
            char *p = (char *)realloc(buf, nc);
            if (!p) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = p;
            cap = nc;
        }
        size_t got = fread(buf + len, 1, cap - len, f);
        if (got == 0) {
            break;
        }
        len += got;
    }
    fclose(f);
    *out_len = len;
    return buf;
}

static struct yetty_ymarkdown_render_config default_config(float pane_w, float pane_h)
{
    /* Approximate monospace cell metrics — ymarkdown derives the body
     * font size from cell_height. Width/height in cells round down so
     * the rendered text fits inside the pane without horizontal clip. */
    const uint32_t cell_w = 8;
    const uint32_t cell_h = 16;
    uint32_t wc = pane_w > (float)cell_w ? (uint32_t)(pane_w / (float)cell_w) : 1u;
    uint32_t hc = pane_h > (float)cell_h ? (uint32_t)(pane_h / (float)cell_h) : 1u;
    struct yetty_ymarkdown_render_config cfg = {
        .cell_width = cell_w,
        .cell_height = cell_h,
        .width_cells = wc,
        .height_cells = hc,
    };
    return cfg;
}

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

struct yetty_ygui_widget *yetty_ygui_engine_ymarkdown_from_file(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h, const char *path)
{
    if (!path) {
        return NULL;
    }
    size_t len = 0;
    char *bytes = slurp_file(path, &len);
    if (!bytes) {
        return NULL;
    }
    struct yetty_ymarkdown_render_config cfg = default_config(w, h);
    struct yetty_ymarkdown_render_result r =
        yetty_ymarkdown_render(bytes, len, NULL, 0, &cfg);
    free(bytes);
    return attach_to_rich(engine, id, x, y, w, h, r);
}

struct yetty_ygui_widget *yetty_ygui_engine_ymarkdown_from_buffer(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const uint8_t *data, size_t len)
{
    struct yetty_ymarkdown_render_config cfg = default_config(w, h);
    struct yetty_ymarkdown_render_result r =
        yetty_ymarkdown_render((const char *)data, len, NULL, 0, &cfg);
    return attach_to_rich(engine, id, x, y, w, h, r);
}
