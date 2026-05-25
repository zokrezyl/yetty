/*
 * ygui_ybrowser.c — yetty browser (HTML/CSS) widget.
 *
 * Render path: ylexbor (load_html → render) → draw_list → RICH widget.
 *
 * Both constructors set ylexbor's base URL so external <link>/<script>/
 * <img> references resolve relative to a local directory:
 *   - from_file: base is file://<absolute dir of path>/, derived here.
 *   - from_buffer: caller passes base_url explicitly (may be NULL).
 *
 * Fetching of the referenced resources is handled inside ylexbor (libcss
 * + libcurl); we just hand it the base.
 */

#include <yetty/ygui-old/ygui.h>
#include <yetty/ygui-old/ygui_ybrowser.h>
#include <yetty/ybrowser/ybrowser.h>
#include <yetty/ydraw-core/draw-list.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Build "file://<absolute dir>/" from `path`. Returned string is
 * caller-owned (free). Returns NULL on alloc / realpath failure. */
static char *base_url_from_file(const char *path)
{
    char abspath[PATH_MAX];
    if (!realpath(path, abspath)) {
        return NULL;
    }
    char *slash = strrchr(abspath, '/');
    if (slash) {
        slash[1] = '\0'; /* keep trailing slash */
    } else {
        /* No directory component — treat as cwd. */
        abspath[0] = '.';
        abspath[1] = '/';
        abspath[2] = '\0';
    }
    size_t need = strlen("file://") + strlen(abspath) + 1;
    char *url = (char *)malloc(need);
    if (!url) {
        return NULL;
    }
    snprintf(url, need, "file://%s", abspath);
    return url;
}

static struct yetty_ygui_old_widget *render_with_base(struct yetty_ygui_old_engine *engine, const char *id,
                                                  float x, float y, float w, float h,
                                                  const char *html, size_t html_len,
                                                  const char *base_url)
{
    struct yetty_ylexbor_config cfg = {
        .viewport_width = (int)w,
        .viewport_height = (int)h,
        .default_font_size = 16.0f,
    };
    struct yetty_ylexbor_ptr_result lr = yetty_ylexbor_create(&cfg);
    if (YETTY_IS_ERR(lr)) {
        yetty_ycore_error_destroy(lr.error);
        return NULL;
    }
    struct yetty_ylexbor *yl = lr.value;

    if (base_url && *base_url) {
        struct yetty_ycore_void_result br = yetty_ylexbor_set_base_url(yl, base_url);
        if (YETTY_IS_ERR(br)) {
            yetty_ycore_error_destroy(br.error);
        }
    }

    struct yetty_ycore_void_result hr = yetty_ylexbor_load_html(yl, html, html_len);
    if (YETTY_IS_ERR(hr)) {
        yetty_ycore_error_destroy(hr.error);
        yetty_ylexbor_destroy(yl);
        return NULL;
    }

    struct yetty_ydraw_draw_list_result br = yetty_ydraw_draw_list_config_buffer_create(NULL);
    if (YETTY_IS_ERR(br)) {
        yetty_ycore_error_destroy(br.error);
        yetty_ylexbor_destroy(yl);
        return NULL;
    }
    struct yetty_ydraw_draw_list *buf = br.value;

    struct yetty_ycore_void_result rr = yetty_ylexbor_render(yl, buf);
    yetty_ylexbor_destroy(yl);
    if (YETTY_IS_ERR(rr)) {
        yetty_ycore_error_destroy(rr.error);
        yetty_ydraw_draw_list_destroy(buf);
        return NULL;
    }

    struct yetty_ygui_old_widget *widget = yetty_ygui_old_engine_rich(engine, id, x, y, w, h);
    if (!widget) {
        yetty_ydraw_draw_list_destroy(buf);
        return NULL;
    }
    yetty_ygui_old_widget_rich_set_buffer(widget, buf);
    return widget;
}

struct yetty_ygui_old_widget *yetty_ygui_old_engine_ybrowser_from_file(struct yetty_ygui_old_engine *engine,
                                                               const char *id, float x, float y,
                                                               float w, float h, const char *path)
{
    if (!path) {
        return NULL;
    }
    size_t html_len = 0;
    char *html = slurp_file(path, &html_len);
    if (!html) {
        return NULL;
    }
    char *base = base_url_from_file(path);
    struct yetty_ygui_old_widget *widget =
        render_with_base(engine, id, x, y, w, h, html, html_len, base);
    free(html);
    free(base);
    return widget;
}

struct yetty_ygui_old_widget *yetty_ygui_old_engine_ybrowser_from_buffer(struct yetty_ygui_old_engine *engine,
                                                                 const char *id, float x, float y,
                                                                 float w, float h,
                                                                 const uint8_t *data, size_t len,
                                                                 const char *base_url)
{
    if (!data || len == 0) {
        return NULL;
    }
    return render_with_base(engine, id, x, y, w, h, (const char *)data, len, base_url);
}

/* Built-in default sample (demo/ygui/26_ybrowser/sample.html), baked
 * into the library via ygui_embed_default_asset (incbin / RCDATA). */
#include "ygui_ybrowser_ybrowser_default_manifest.h"

static const uint8_t *g_ybrowser_default_data = NULL;
static size_t g_ybrowser_default_size = 0;
static void ybrowser_capture_default(const char *name, const uint8_t *data, size_t size,
                                     int compressed)
{
    (void)name;
    (void)compressed;
    g_ybrowser_default_data = data;
    g_ybrowser_default_size = size;
}

struct yetty_ygui_old_widget *yetty_ygui_old_engine_ybrowser_default(struct yetty_ygui_old_engine *engine,
                                                             const char *id, float x, float y,
                                                             float w, float h)
{
    if (!g_ybrowser_default_data) {
        register_ybrowser_default_assets_c(ybrowser_capture_default);
    }
    /* base_url=NULL — sample.html is self-contained. */
    return yetty_ygui_old_engine_ybrowser_from_buffer(engine, id, x, y, w, h, g_ybrowser_default_data,
                                                  g_ybrowser_default_size, NULL);
}
