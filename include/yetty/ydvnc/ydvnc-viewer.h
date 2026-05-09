#ifndef YETTY_YDVNC_VIEWER_H
#define YETTY_YDVNC_VIEWER_H

/*
 * ydvnc — desktop VNC client (RFB 3.8 / RFC 6143).
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

/* Password may be NULL — only None-auth servers will then connect. */
struct yetty_ydvnc_viewer_ptr_result yetty_ydvnc_viewer_create(
    const char *host, uint16_t port, const char *password, const struct yetty_context *yetty_ctx);

struct yetty_ycore_void_result yetty_ydvnc_viewer_destroy(struct yetty_ydvnc_viewer *viewer);

struct yetty_yterm_view *yetty_ydvnc_viewer_as_view(struct yetty_ydvnc_viewer *viewer);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YDVNC_VIEWER_H */
