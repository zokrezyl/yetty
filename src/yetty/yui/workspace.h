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

/* Create/destroy. The default form auto-allocates the workspace id; the
 * _with_id form is used by chrome to pre-mint an id (via
 * yetty_ycore_next_object_id) so its widget map and the workspace agree
 * on the identifier without a discovery round-trip. */
struct yetty_yui_workspace_ptr_result yetty_yui_workspace_create(void);

struct yetty_yui_workspace_ptr_result yetty_yui_workspace_create_with_id(
    yetty_ycore_object_id id);

struct yetty_ycore_void_result yetty_yui_workspace_destroy(struct yetty_yui_workspace *ws);

/* Accessor for the workspace's own id (the one chrome minted, or the
 * auto-allocated fallback). 0 when NULL. */
yetty_ycore_object_id yetty_yui_workspace_id(const struct yetty_yui_workspace *ws);

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

/* Chrome-driven split: target_pane_id is the pane to split, new_pane_id
 * and new_split_id are the ids chrome pre-minted for the fresh tiles.
 * Mirrors yetty_yui_workspace_split_pane except the new tiles' ids are
 * fixed by the caller. */
struct yetty_ycore_void_result yetty_yui_workspace_split_pane_with_ids(
    struct yetty_yui_workspace *ws, yetty_ycore_object_id target_pane_id,
    yetty_ycore_object_id new_pane_id, yetty_ycore_object_id new_split_id,
    enum yetty_yui_orientation orientation);

/* Chrome reports a splitter drag — update split_id's ratio and re-lay
 * out. No-op if split_id isn't found. */
struct yetty_ycore_void_result yetty_yui_workspace_resize_split(
    struct yetty_yui_workspace *ws, yetty_ycore_object_id split_id, float ratio);

/* Append an empty pane to a workspace that has no root yet. The pane is
 * created with the caller-supplied id (chrome pre-mints it). Today only
 * used for the first-pane-in-a-fresh-workspace path. */
struct yetty_ycore_void_result yetty_yui_workspace_create_first_pane(
    struct yetty_yui_workspace *ws, yetty_ycore_object_id pane_id);

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
