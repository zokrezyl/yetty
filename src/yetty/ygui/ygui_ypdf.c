/*
 * ygui_ypdf.c — yetty PDF widget.
 *
 * Render path: pdfioFileOpen → ypdf → draw_list → RICH widget.
 *
 * pdfio only exposes filename-keyed entry points, so the from_buffer
 * variant writes the bytes to a tmp file, opens it, then unlinks it.
 * The on-disk window is microseconds; the price for one extra fopen
 * is keeping the public widget surface simple.
 */

#include <yetty/ygui/ygui.h>
#include <yetty/ygui/ygui_ypdf.h>
#include <yetty/ypdf/ypdf.h>

#include <pdfio.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

YETTY_EXTERNAL_CALLBACK
static bool pdfio_silent(pdfio_file_t *pdf, const char *message, void *data)
{
    (void)pdf;
    (void)message;
    (void)data;
    /* Suppressed — widget surface has nowhere to surface a libpdfio
     * non-fatal warning. The caller already gets NULL on hard failures. */
    return true;
}

static struct yetty_ygui_widget *attach_from_pdf(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h, struct _pdfio_file_s *pdf)
{
    if (!pdf) {
        return NULL;
    }
    struct yetty_ypdf_render_result rr = yetty_ypdf_render_pdf(pdf);
    pdfioFileClose(pdf);
    if (YETTY_IS_ERR(rr)) {
        yetty_ycore_error_destroy(rr.error);
        return NULL;
    }
    struct yetty_ygui_widget *widget = yetty_ygui_engine_rich(engine, id, x, y, w, h);
    if (!widget) {
        yetty_ydraw_draw_list_destroy(rr.value.buffer);
        return NULL;
    }
    yetty_ygui_widget_rich_set_buffer(widget, rr.value.buffer);
    return widget;
}

struct yetty_ygui_widget *yetty_ygui_engine_ypdf_from_file(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h, const char *path)
{
    if (!path) {
        return NULL;
    }
    pdfio_file_t *pdf = pdfioFileOpen(path, NULL, NULL, pdfio_silent, NULL);
    return attach_from_pdf(engine, id, x, y, w, h, pdf);
}

struct yetty_ygui_widget *yetty_ygui_engine_ypdf_from_buffer(
    struct yetty_ygui_engine *engine, const char *id,
    float x, float y, float w, float h,
    const uint8_t *data, size_t len)
{
    if (!data || len == 0) {
        return NULL;
    }
    char tmpl[] = "/tmp/yetty-ygui-ypdf-XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        return NULL;
    }
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, data + written, len - written);
        if (n <= 0) {
            close(fd);
            unlink(tmpl);
            return NULL;
        }
        written += (size_t)n;
    }
    close(fd);

    pdfio_file_t *pdf = pdfioFileOpen(tmpl, NULL, NULL, pdfio_silent, NULL);
    /* Unlink straight away — pdfio holds the file open, the inode
     * survives until pdfioFileClose. No leftover temp on crash. */
    unlink(tmpl);
    return attach_from_pdf(engine, id, x, y, w, h, pdf);
}
