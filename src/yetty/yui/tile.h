#ifndef YETTY_YUI_TILE_H
#define YETTY_YUI_TILE_H

#include <stddef.h>
#include <stdint.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yui-core/view.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yui_tile;
struct yetty_yconfig_config;
struct yetty_context;
struct yetty_yui_event;
struct yetty_ydraw_target;

/* Result types */
YETTY_YRESULT_DECLARE(yetty_yui_tile_ptr, struct yetty_yui_tile *);

/* Orientation for splits */
enum yetty_yui_orientation {
    YETTY_YUI_HORIZONTAL,
    YETTY_YUI_VERTICAL,
};

/* Tile type */
enum yetty_yui_tile_type {
    YETTY_YUI_TILE_SPLIT,
    YETTY_YUI_TILE_PANE,
};

/* Create/destroy */
struct yetty_yui_tile_ptr_result yetty_yui_split_create(enum yetty_yui_orientation orientation);

struct yetty_yui_tile_ptr_result yetty_yui_pane_create(void);

/* Chrome-driven creation: caller mints the id (via
 * yetty_ycore_next_object_id) and the tile is born with that exact id.
 * Used by yui to pre-allocate ids before posting PANE_CREATE / PANE_SPLIT
 * events so both sides agree on the identifier without a discovery
 * round-trip. */
struct yetty_yui_tile_ptr_result yetty_yui_split_create_with_id(
    yetty_ycore_object_id id, enum yetty_yui_orientation orientation);

struct yetty_yui_tile_ptr_result yetty_yui_pane_create_with_id(yetty_ycore_object_id id);

/* Create tile tree from config - recursively builds splits/panes */
struct yetty_yui_tile_ptr_result yetty_yui_tile_create_from_config(
    const struct yetty_yconfig_config *config, const struct yetty_context *yetty_ctx);

struct yetty_ycore_void_result yetty_yui_tile_destroy(struct yetty_yui_tile *tile);

/* Tile operations */
struct yetty_ycore_void_result yetty_yui_tile_render(
    struct yetty_yui_tile *tile, struct yetty_ydraw_target *render_target);

struct yetty_ycore_void_result yetty_yui_tile_set_bounds(struct yetty_yui_tile *tile,
                                                         struct yetty_yui_rect bounds);

/* Event handling - returns 1 if handled, 0 if not */
struct yetty_ycore_int_result yetty_yui_tile_on_event(struct yetty_yui_tile *tile,
                                                      const struct yetty_yui_event *event);

/* Accessors */
yetty_ycore_object_id yetty_yui_tile_id(const struct yetty_yui_tile *tile);
enum yetty_yui_tile_type yetty_yui_tile_type(const struct yetty_yui_tile *tile);
struct yetty_yui_rect yetty_yui_tile_bounds(const struct yetty_yui_tile *tile);

/* Split-specific */
struct yetty_ycore_void_result yetty_yui_tile_split_set_first(struct yetty_yui_tile *split,
                                                              struct yetty_yui_tile *tile);

struct yetty_ycore_void_result yetty_yui_tile_split_set_second(struct yetty_yui_tile *split,
                                                               struct yetty_yui_tile *tile);

struct yetty_ycore_void_result yetty_yui_tile_split_set_ratio(struct yetty_yui_tile *split,
                                                              float ratio);

struct yetty_yui_tile *yetty_yui_tile_split_first(const struct yetty_yui_tile *split);
struct yetty_yui_tile *yetty_yui_tile_split_second(const struct yetty_yui_tile *split);
float yetty_yui_tile_split_ratio(const struct yetty_yui_tile *split);
enum yetty_yui_orientation yetty_yui_tile_split_orientation(const struct yetty_yui_tile *split);

/* Pane-specific */
struct yetty_ycore_void_result yetty_yui_tile_pane_push_view(struct yetty_yui_tile *pane,
                                                             struct yetty_yui_view *view);

struct yetty_ycore_void_result yetty_yui_tile_pane_pop_view(struct yetty_yui_tile *pane);

struct yetty_yui_view *yetty_yui_tile_pane_active_view(const struct yetty_yui_tile *pane);

size_t yetty_yui_tile_pane_view_count(const struct yetty_yui_tile *pane);

int yetty_yui_tile_pane_has_view(const struct yetty_yui_tile *pane, yetty_ycore_object_id view_id);

int yetty_yui_tile_pane_focused(const struct yetty_yui_tile *pane);

void yetty_yui_tile_pane_set_focused(struct yetty_yui_tile *pane, int focused);

/* Tree helpers */
struct yetty_yui_tile *yetty_yui_tile_find_by_id(struct yetty_yui_tile *root,
                                                 yetty_ycore_object_id id);

struct yetty_yui_tile *yetty_yui_tile_find_parent_split(struct yetty_yui_tile *root,
                                                        yetty_ycore_object_id target_id);

struct yetty_yui_tile *yetty_yui_tile_find_focused_pane(struct yetty_yui_tile *root);

/* Find pane at coordinates (hit test) */
struct yetty_yui_tile *yetty_yui_tile_find_pane_at(struct yetty_yui_tile *root, float x, float y);

/* Find first pane in tree (depth-first) */
struct yetty_yui_tile *yetty_yui_tile_find_first_pane(struct yetty_yui_tile *root);

/* Clear focus from all panes in tree */
void yetty_yui_tile_clear_focus(struct yetty_yui_tile *root);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YUI_TILE_H */
