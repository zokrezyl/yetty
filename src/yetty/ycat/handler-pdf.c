/*
 * handler-pdf.c - PDF → ypaint buffer.
 *
 * pdfio only opens PDFs from a filename. When we already have a path_hint
 * we pass it straight through. When bytes come from stdin / URL we spill
 * them to a mkstemp file, render, then unlink.
 */

#include <yetty/ycat/ycat.h>

#include <yetty/ypdf/ypdf.h>
#include <yetty/ytrace.h>

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
    /* MSVC: GetTempPath + GetTempFileName. The latter creates a 0-byte
	 * file on disk and returns a unique path under it. */
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
 * Handler
 *===========================================================================*/

static struct yetty_ypaint_core_buffer_result render_from_path(const char *path)
{
    struct _pdfio_file_s *pdf = pdfioFileOpen(path, NULL, NULL, NULL, NULL);
    if (!pdf) {
        return YETTY_ERR(yetty_ypaint_core_buffer, "pdfioFileOpen failed");
    }

    struct yetty_ypdf_render_result r = yetty_ypdf_render_pdf(pdf);
    pdfioFileClose(pdf);

    if (YETTY_IS_ERR(r)) {
        return YETTY_ERR(yetty_ypaint_core_buffer, r.error.msg);
    }
    return YETTY_OK(yetty_ypaint_core_buffer, r.value.buffer);
}

struct yetty_ypaint_core_buffer_result yetty_ycat_handler_pdf(const uint8_t *bytes, size_t len,
                                                        const char *path_hint,
                                                        const struct yetty_ycat_config *config)
{
    (void)config;

    if (path_hint && *path_hint) {
        return render_from_path(path_hint);
    }

    if (!bytes || len == 0) {
        return YETTY_ERR(yetty_ypaint_core_buffer, "no bytes and no path");
    }

    char tmp_path[256];
    if (spill_to_tempfile(bytes, len, tmp_path, sizeof(tmp_path)) < 0) {
        return YETTY_ERR(yetty_ypaint_core_buffer, "failed to spill PDF to temp file");
    }

    struct yetty_ypaint_core_buffer_result r = render_from_path(tmp_path);
    unlink(tmp_path);
    return r;
}
