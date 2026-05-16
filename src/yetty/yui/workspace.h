#ifndef YETTY_YUI_WORKSPACE_H
#define YETTY_YUI_WORKSPACE_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yui/tile.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yui_workspace;
struct yetty_yconfig_config;
struct yetty_context;
struct yetty_yui_event;
struct yetty_ydraw_target;

/* Result types */
YETTY_YRESULT_DECLARE(yetty_yui_workspace_ptr, struct yetty_yui_workspace *);

/* Create/destroy */
struct yetty_yui_workspace_ptr_result yetty_yui_workspace_create(void);

struct yetty_ycore_void_result yetty_yui_workspace_destroy(struct yetty_yui_workspace *ws);

/* Core operations */
struct yetty_ycore_void_result yetty_yui_workspace_render(
    struct yetty_yui_workspace *ws, struct yetty_ydraw_target *render_target);

struct yetty_ycore_void_result yetty_yui_workspace_resize(struct yetty_yui_workspace *ws,
                                                          float width, float height);

/* Place this workspace's top-left at (x, y) inside the render target. The
 * tabbar sets y = tab strip height so the workspace's tiles render below
 * the strip instead of starting at y=0 and being overdrawn. Call before
 * workspace_resize / workspace_set_root so the first set_bounds already
 * lands at the right origin. */
struct yetty_ycore_void_result yetty_yui_workspace_set_origin(struct yetty_yui_workspace *ws,
                                                              float x, float y);

/* Mark this workspace as the tabbar's active one (active != 0) or as
 * deactivated (active == 0), and propagate the change DOWN to the
 * active view. Concretely:
 *   - if active and no pane in the tree is focused yet, focus the
 *     first pane (so the cascade has a target);
 *   - dispatch a SET_FOCUS event to the focused pane, which forwards
 *     it to its active view. event.set_focus.object_id carries the
 *     pane's id when active, 0 when deactivated, so leaf views (e.g.
 *     the terminal) can update their internal focused state.
 *
 * The tabbar calls this on every tab switch — without it the per-pane
 * `focused` flag is set at load_layout time but nothing ever tells the
 * leaf view (the terminal) that it is the foreground one, and effects
 * that depend on that knowledge (focus-reporting CSEQ, cursor blink
 * style, etc.) never fire on tab change. */
struct yetty_ycore_void_result yetty_yui_workspace_set_active(struct yetty_yui_workspace *ws,
                                                              int active);

/* Root tile management */
struct yetty_ycore_void_result yetty_yui_workspace_set_root(struct yetty_yui_workspace *ws,
                                                            struct yetty_yui_tile *tile);

struct yetty_yui_tile *yetty_yui_workspace_root(const struct yetty_yui_workspace *ws);

/* Accessors */
float yetty_yui_workspace_width(const struct yetty_yui_workspace *ws);
float yetty_yui_workspace_height(const struct yetty_yui_workspace *ws);

/* Tree operations */
struct yetty_ycore_void_result yetty_yui_workspace_split_pane(
    struct yetty_yui_workspace *ws, yetty_ycore_object_id pane_id,
    enum yetty_yui_orientation orientation);

struct yetty_ycore_void_result yetty_yui_workspace_close_tile(struct yetty_yui_workspace *ws,
                                                              yetty_ycore_object_id tile_id);

/* Load layout from config - creates tile tree and sets as root */
struct yetty_ycore_void_result yetty_yui_workspace_load_layout(
    struct yetty_yui_workspace *ws, const struct yetty_yconfig_config *config,
    const struct yetty_context *yetty_ctx);

/* Event handling - returns 1 if handled, 0 if not */
struct yetty_ycore_int_result yetty_yui_workspace_on_event(struct yetty_yui_workspace *ws,
                                                           const struct yetty_yui_event *event);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YUI_WORKSPACE_H */
