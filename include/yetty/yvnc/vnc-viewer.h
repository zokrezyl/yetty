#ifndef YETTY_YVNC_VNC_VIEWER_H
#define YETTY_YVNC_VNC_VIEWER_H

#include <yetty/ycore/result.h>
#include <yetty/yui/view.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yvnc_viewer;
struct yetty_context;

/* Result type */
YETTY_YRESULT_DECLARE(yetty_vnc_viewer_ptr, struct yetty_yvnc_viewer *);

/* Create VNC viewer - connects to host:port */
struct yetty_vnc_viewer_ptr_result yetty_yvnc_viewer_create(const char *host, uint16_t port,
                                                            const struct yetty_context *yetty_ctx);

/* Destroy viewer */
struct yetty_ycore_void_result yetty_yvnc_viewer_destroy(struct yetty_yvnc_viewer *viewer);

/* Cast viewer to view (for use in tile pane) */
struct yetty_yterm_view *yetty_yvnc_viewer_as_view(struct yetty_yvnc_viewer *viewer);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVNC_VNC_VIEWER_H */
