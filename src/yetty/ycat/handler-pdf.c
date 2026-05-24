/*
 * handler-pdf.c - PDF → ydraw envelope stream (one envelope per page).
 *
 * pdfio only opens PDFs from a filename. When we already have a path_hint
 * we pass it straight through. When bytes come from stdin / URL we spill
 * them to a mkstemp file, render, then unlink.
 *
 * The renderer streams pages: each page produces a fresh ydraw buffer
 * with envelope-local coordinates (origin at y=0). Fonts are content-
 * addressed by FNV1a64(TTF); the first envelope shipping a given font
 * carries the full bytes, later envelopes ship a hash-only FONT prim.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/ypdf/ypdf.h>
#include <yetty/ytrace/ytrace.h>

#include <pdfio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h> /* GetTempPathA / GetTempFileNameA / MAX_PATH */
#include <io.h>
#include <fcntl.h> /* _O_WRONLY / _O_BINARY for _open */
typedef long long ssize_t;
#define write(fd, buf, n) _write((fd), (buf), (unsigned int)(n))
#define close _close
#define unlink _unlink
#else
#include <unistd.h>
#endif

/*=============================================================================
 * Spill bytes to temp file
 *===========================================================================*/

/* Writes bytes to a fresh tempfile; stores the path in `path_out` (must be
 * at least sz_out bytes). Returns 0 on success, -1 on failure. */
static int spill_to_tempfile(const uint8_t *bytes, size_t len, char *path_out, size_t sz_out)
{
#ifdef _WIN32
    char tmpdir[MAX_PATH];
    if (GetTempPathA((DWORD)sizeof(tmpdir), tmpdir) == 0) {
        return -1;
    }
    if (GetTempFileNameA(tmpdir, "ycat", 0, path_out) == 0) {
        return -1;
    }
    int fd = _open(path_out, _O_WRONLY | _O_BINARY);
    if (fd < 0) {
        return -1;
    }
#else
    const char *tmpdir = getenv("TMPDIR");
    if (!tmpdir || !*tmpdir) {
        tmpdir = "/tmp";
    }

    int n = snprintf(path_out, sz_out, "%s/ycat-pdf-XXXXXX", tmpdir);
    if (n < 0 || (size_t)n >= sz_out) {
        return -1;
    }

    int fd = mkstemp(path_out);
    if (fd < 0) {
        return -1;
    }
#endif

    size_t written = 0;
    while (written < len) {
        ssize_t w = write(fd, bytes + written, len - written);
        if (w < 0) {
            close(fd);
            unlink(path_out);
            return -1;
        }
        written += (size_t)w;
    }
    close(fd);
    return 0;
}

/*=============================================================================
 * Streaming bridge: ypdf page callback → ycat emit callback
 *===========================================================================*/

struct page_bridge_ctx {
    yetty_ycat_emit_fn emit;
    void *emit_ud;
};

static struct yetty_ycore_void_result page_to_ycat_emit(
    void *ud, int page_index, int page_count, const struct yetty_ydraw_draw_list *envelope)
{
    (void)page_index;
    (void)page_count;
    struct page_bridge_ctx *b = ud;
    struct yetty_ycore_void_result r = b->emit(b->emit_ud, envelope);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "ycat emit failed");
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Handler
 *===========================================================================*/

static struct yetty_ycore_void_result stream_from_path(const char *path, yetty_ycat_emit_fn emit,
                                                       void *emit_ud)
{
    struct _pdfio_file_s *pdf = pdfioFileOpen(path, NULL, NULL, NULL, NULL);
    if (!pdf) {
        return YETTY_ERR(yetty_ycore_void, "pdfioFileOpen failed");
    }

    struct page_bridge_ctx bridge = {.emit = emit, .emit_ud = emit_ud};
    struct yetty_ypdf_stream_render_result r =
        yetty_ypdf_render_pdf_streaming(pdf, page_to_ycat_emit, &bridge);
    pdfioFileClose(pdf);

    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ycore_void, "ypdf streaming render failed", r);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ycat_handler_pdf_streaming(
    const uint8_t *bytes, size_t len, const char *path_hint, const struct yetty_ycat_config *config,
    yetty_ycat_emit_fn emit, void *emit_user_data)
{
    (void)config;

    if (!emit) {
        return YETTY_ERR(yetty_ycore_void, "emit callback is NULL");
    }

    if (path_hint && *path_hint) {
        return stream_from_path(path_hint, emit, emit_user_data);
    }

    if (!bytes || len == 0) {
        return YETTY_ERR(yetty_ycore_void, "no bytes and no path");
    }

    char tmp_path[256];
    if (spill_to_tempfile(bytes, len, tmp_path, sizeof(tmp_path)) < 0) {
        return YETTY_ERR(yetty_ycore_void, "failed to spill PDF to temp file");
    }

    struct yetty_ycore_void_result r = stream_from_path(tmp_path, emit, emit_user_data);
    unlink(tmp_path);
    return r;
}
