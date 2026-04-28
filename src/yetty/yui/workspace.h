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
struct yetty_yconfig;
struct yetty_context;
struct yetty_ycore_event;
struct yetty_yrender_target;

/* Result types */
YETTY_YRESULT_DECLARE(yetty_yui_workspace_ptr, struct yetty_yui_workspace *);

/* Create/destroy */
struct yetty_yui_workspace_ptr_result yetty_yui_workspace_create(void);

void yetty_yui_workspace_destroy(struct yetty_yui_workspace *ws);

/* Core operations */
struct yetty_ycore_void_result yetty_yui_workspace_render(
    struct yetty_yui_workspace *ws, struct yetty_yrender_target *render_target);

struct yetty_ycore_void_result yetty_yui_workspace_resize(struct yetty_yui_workspace *ws,
                                                          float width, float height);

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
    struct yetty_yui_workspace *ws, const struct yetty_yconfig *config,
    const struct yetty_context *yetty_ctx);

/* Event handling - returns 1 if handled, 0 if not */
struct yetty_ycore_int_result yetty_yui_workspace_on_event(struct yetty_yui_workspace *ws,
                                                           const struct yetty_ycore_event *event);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YUI_WORKSPACE_H */
