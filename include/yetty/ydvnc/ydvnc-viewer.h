#ifndef YETTY_YDVNC_VIEWER_H
#define YETTY_YDVNC_VIEWER_H

/*
 * ydvnc — desktop VNC client speaking the standard RFB protocol (RFC 6143)
 * with the Tight encoding (TurboJPEG sub-encoding). Talks to TigerVNC /
 * TurboVNC servers.
 *
 * Implemented from the public RFB spec (RFC 6143 + github.com/rfbproto).
 * No code from TigerVNC, TurboVNC, libvncclient, or noVNC was consulted.
 * Use build-tools/check-ydvnc-originality.sh as a tripwire before committing.
 */

#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/yui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydvnc_viewer;
struct yetty_context;

YETTY_YRESULT_DECLARE(yetty_ydvnc_viewer_ptr, struct yetty_ydvnc_viewer *);

struct yetty_ydvnc_viewer_ptr_result yetty_ydvnc_viewer_create(
    const char *host, uint16_t port, const struct yetty_context *yetty_ctx);

struct yetty_ycore_void_result yetty_ydvnc_viewer_destroy(struct yetty_ydvnc_viewer *viewer);

struct yetty_yterm_view *yetty_ydvnc_viewer_as_view(struct yetty_ydvnc_viewer *viewer);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDVNC_VIEWER_H */
