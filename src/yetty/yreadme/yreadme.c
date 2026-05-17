/*
 * yreadme.c — README rendering (currently Markdown only).
 *
 * Wraps ymarkdown with file-slurp + default-config conveniences. Keeps
 * the widget side oblivious to file IO and config knobs.
 */

#include <yetty/yreadme/yreadme.h>

#include <stdio.h>
#include <stdlib.h>

struct yetty_ymarkdown_render_config yetty_yreadme_default_config(float pane_w, float pane_h)
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

struct yetty_ymarkdown_render_result yetty_yreadme_render_from_buffer(
    const uint8_t *data, size_t len,
    const struct yetty_ymarkdown_render_config *config)
{
    struct yetty_ymarkdown_render_config fallback = yetty_yreadme_default_config(800.0f, 600.0f);
    const struct yetty_ymarkdown_render_config *cfg = config ? config : &fallback;
    return yetty_ymarkdown_render((const char *)data, len, NULL, 0, cfg);
}

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

struct yetty_ymarkdown_render_result yetty_yreadme_render_from_file(
    const char *path, const struct yetty_ymarkdown_render_config *config)
{
    if (!path) {
        return YETTY_ERR(yetty_ymarkdown_render, "yreadme: path is NULL");
    }
    size_t len = 0;
    char *bytes = slurp_file(path, &len);
    if (!bytes) {
        return YETTY_ERR(yetty_ymarkdown_render, "yreadme: cannot read file");
    }
    struct yetty_ymarkdown_render_result r =
        yetty_yreadme_render_from_buffer((const uint8_t *)bytes, len, config);
    free(bytes);
    return r;
}
