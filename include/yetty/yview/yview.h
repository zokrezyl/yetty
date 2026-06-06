/*
 * yview — client-side capability for a server-side scrollable content surface.
 *
 * A "view" is one positioned figure under a running yetty's root figure
 * container, shipped over DCS YETTY_DCS_YCOMPOSITOR_BIN (the compositor wire
 * path — NOT the scrolling ydraw layer ycat uses). The content is sent ONCE;
 * scrolling, clipping and repositioning are server-side state thereafter:
 *
 *   - create()       mints a child figure (default kind YGRID) at a pixel rect
 *   - set_content()  ships the drawable-list body as the child's init payload
 *                    and declares the content extent (so the rect becomes a
 *                    scroll viewport when content is taller than the rect)
 *   - scroll_to/_by  move the server-side scroll offset (no body re-ship)
 *   - set_rect()     move/resize the viewport (e.g. an nvim window moved)
 *   - destroy()      drop the figure from the container
 *
 * Content-agnostic: it takes a yetty_ydraw_drawable_list (exactly what the
 * ycat handlers produce), so the same view holds text, SDF prims, or complex
 * figures (yplot/yimage/… via ygrid's composite factory). Two consumers: the
 * `yless` pager tool, and embedders such as an nvim lua plugin that owns a
 * window region and forwards scroll as the user moves within it.
 *
 * Only YGRID-kind views are scrollable (the figure's set_scroll slot); a
 * bare complex-figure kind can be minted for a single, non-scrolling figure.
 */
#ifndef YETTY_YVIEW_YVIEW_H
#define YETTY_YVIEW_YVIEW_H

#include <stdint.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_ydraw_drawable_list;
struct yetty_yview;

YETTY_YRESULT_DECLARE(yetty_yview_ptr, struct yetty_yview *);

/* View option flags. */
enum yetty_yview_flag {
    YETTY_YVIEW_FLAG_NONE = 0u,
    /* Reserved: draw a chrome frame/border around the content. Not yet
     * drawn — a fixed (non-scrolling) frame needs the chrome/absolute-coords
     * path; tracked as a follow-up. */
    YETTY_YVIEW_FLAG_FRAME = 1u << 0,
};

struct yetty_yview_config {
    /* Output fd the envelope bytes are written to (e.g. STDOUT_FILENO when
     * running inside a yetty, directly or via tmux). */
    int fd;
    /* Viewport rectangle in target pixels — the on-screen window. */
    struct yetty_ycore_rectangle rect;
    /* Figure kind (yetty_yfigure_wire_kind). 0 selects the default, YGRID. */
    uint32_t kind;
    /* Bitwise-or of enum yetty_yview_flag. */
    uint32_t flags;
    /* Parent-scoped figure id. 0 lets the library assign a default (1).
     * Callers that may run concurrently (several panes / windows) should pass
     * distinct non-zero ids so their figures don't collide. */
    uint32_t child_id;
};

/* Allocate a view handle. Emits nothing yet — the figure is minted on the
 * first set_content(). */
struct yetty_yview_ptr_result yetty_yview_create(const struct yetty_yview_config *config);

/* Ship the content as the child figure's body (CREATE_CHILD init payload) and
 * declare the content extent from the drawable list's scene bounds. Safe to
 * call again to replace the content (same id reuses the figure in place). */
struct yetty_ycore_void_result yetty_yview_set_content(
    struct yetty_yview *view, const struct yetty_ydraw_drawable_list *content);

/* Override the content extent in px (the rect stays the visible window).
 * Usually unnecessary — set_content() derives it from the scene bounds. */
struct yetty_ycore_void_result yetty_yview_set_content_size(struct yetty_yview *view,
                                                            float content_w, float content_h);

/* Set the absolute scroll offset (content coord shown at the rect top-left).
 * Clamped to the known content extent. */
struct yetty_ycore_void_result yetty_yview_scroll_to(struct yetty_yview *view, float scroll_x,
                                                     float scroll_y);

/* Move the scroll offset by a delta, clamped to the content extent. */
struct yetty_ycore_void_result yetty_yview_scroll_by(struct yetty_yview *view, float delta_x,
                                                     float delta_y);

/* Move/resize the viewport rectangle. */
struct yetty_ycore_void_result yetty_yview_set_rect(struct yetty_yview *view,
                                                    struct yetty_ycore_rectangle rect);

/* Drop the figure from the container and free the handle. Handles NULL. */
struct yetty_ycore_void_result yetty_yview_destroy(struct yetty_yview *view);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YVIEW_YVIEW_H */
