#include <yetty/yui/workspace.h>
#include <yetty/yui/tile.h>
#include <yetty/yui/view.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterm/terminal.h>
#include <yetty/yvnc/vnc-viewer.h>
#include <yetty/ydvnc/ydvnc-viewer.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Internal workspace structure
 *===========================================================================*/

struct yetty_yui_workspace {
    struct yetty_yui_tile *root;
    float width;
    float height;
};

/*=============================================================================
 * Create/destroy
 *===========================================================================*/

struct yetty_yui_workspace_ptr_result yetty_yui_workspace_create(void)
{
    struct yetty_yui_workspace *ws;

    ws = calloc(1, sizeof(struct yetty_yui_workspace));
    if (!ws) {
        return YETTY_ERR(yetty_yui_workspace_ptr, "allocation failed");
    }

    return YETTY_OK(yetty_yui_workspace_ptr, ws);
}

struct yetty_ycore_void_result yetty_yui_workspace_destroy(struct yetty_yui_workspace *ws)
{
    struct yetty_ycore_void_result root_err = YETTY_OK_VOID();

    if (!ws) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yui_workspace_destroy: NULL workspace");
    }

    if (ws->root) {
        root_err = yetty_yui_tile_destroy(ws->root);
    }

    free(ws);

    if (YETTY_IS_ERR(root_err)) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yui_workspace_destroy: root destroy failed",
                         root_err);
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Core operations
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yui_workspace_render(
    struct yetty_yui_workspace *ws, struct yetty_ypaint_core_target *render_target)
{
    if (!ws) {
        return YETTY_ERR(yetty_ycore_void, "workspace is NULL");
    }

    if (ws->root) {
        return yetty_yui_tile_render(ws->root, render_target);
    }

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_workspace_resize(struct yetty_yui_workspace *ws,
                                                          float width, float height)
{
    struct yetty_yui_rect bounds;

    if (!ws) {
        return YETTY_ERR(yetty_ycore_void, "workspace is NULL");
    }

    ws->width = width;
    ws->height = height;

    if (ws->root) {
        bounds = (struct yetty_yui_rect){0, 0, width, height};
        return yetty_yui_tile_set_bounds(ws->root, bounds);
    }

    return YETTY_OK_VOID();
}

/*=============================================================================
 * Root tile management
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yui_workspace_set_root(struct yetty_yui_workspace *ws,
                                                            struct yetty_yui_tile *tile)
{
    struct yetty_yui_rect bounds;

    if (!ws) {
        return YETTY_ERR(yetty_ycore_void, "workspace is NULL");
    }

    ws->root = tile;

    if (tile && ws->width > 0 && ws->height > 0) {
        bounds = (struct yetty_yui_rect){0, 0, ws->width, ws->height};
        return yetty_yui_tile_set_bounds(tile, bounds);
    }

    return YETTY_OK_VOID();
}

struct yetty_yui_tile *yetty_yui_workspace_root(const struct yetty_yui_workspace *ws)
{
    return ws ? ws->root : NULL;
}

float yetty_yui_workspace_width(const struct yetty_yui_workspace *ws)
{
    return ws ? ws->width : 0;
}

float yetty_yui_workspace_height(const struct yetty_yui_workspace *ws)
{
    return ws ? ws->height : 0;
}

/*=============================================================================
 * Tree operations
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yui_workspace_split_pane(
    struct yetty_yui_workspace *ws, yetty_ycore_object_id pane_id,
    enum yetty_yui_orientation orientation)
{
    struct yetty_yui_tile *target;
    struct yetty_yui_tile *parent_split;
    struct yetty_yui_tile_ptr_result split_res;
    struct yetty_yui_tile_ptr_result new_pane_res;
    struct yetty_yui_tile *split;
    struct yetty_yui_tile *new_pane;
    struct yetty_ycore_void_result res;

    if (!ws) {
        return YETTY_ERR(yetty_ycore_void, "workspace is NULL");
    }
    if (!ws->root) {
        return YETTY_ERR(yetty_ycore_void, "no root tile");
    }

    /* Find target pane */
    target = yetty_yui_tile_find_by_id(ws->root, pane_id);
    if (!target) {
        return YETTY_ERR(yetty_ycore_void, "pane not found");
    }

    /* Create new split */
    split_res = yetty_yui_split_create(orientation);
    if (YETTY_IS_ERR(split_res)) {
        return YETTY_ERR(yetty_ycore_void, split_res.error.msg);
    }
    split = split_res.value;

    /* Create new pane */
    new_pane_res = yetty_yui_pane_create();
    if (YETTY_IS_ERR(new_pane_res)) {
        yetty_yui_tile_destroy(split);
        return YETTY_ERR(yetty_ycore_void, new_pane_res.error.msg);
    }
    new_pane = new_pane_res.value;

    /* Set up split children */
    res = yetty_yui_tile_split_set_first(split, target);
    if (YETTY_IS_ERR(res)) {
        yetty_yui_tile_destroy(split);
        yetty_yui_tile_destroy(new_pane);
        return res;
    }

    res = yetty_yui_tile_split_set_second(split, new_pane);
    if (YETTY_IS_ERR(res)) {
        yetty_yui_tile_destroy(split);
        yetty_yui_tile_destroy(new_pane);
        return res;
    }

    /* Replace target in tree */
    if (ws->root == target) {
        /* Target was root */
        return yetty_yui_workspace_set_root(ws, split);
    }

    /* Find parent and replace */
    parent_split = yetty_yui_tile_find_parent_split(ws->root, pane_id);
    if (!parent_split) {
        yetty_yui_tile_destroy(split);
        return YETTY_ERR(yetty_ycore_void, "parent split not found");
    }

    if (yetty_yui_tile_split_first(parent_split) == target) {
        res = yetty_yui_tile_split_set_first(parent_split, split);
    } else {
        res = yetty_yui_tile_split_set_second(parent_split, split);
    }

    if (YETTY_IS_ERR(res)) {
        return res;
    }

    /* Re-layout */
    if (ws->width > 0 && ws->height > 0) {
        struct yetty_yui_rect bounds = {0, 0, ws->width, ws->height};
        return yetty_yui_tile_set_bounds(ws->root, bounds);
    }

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_workspace_close_tile(struct yetty_yui_workspace *ws,
                                                              yetty_ycore_object_id tile_id)
{
    struct yetty_yui_tile *parent_split;
    struct yetty_yui_tile *sibling;
    struct yetty_yui_tile *grandparent;
    struct yetty_ycore_void_result res;

    if (!ws) {
        return YETTY_ERR(yetty_ycore_void, "workspace is NULL");
    }
    if (!ws->root) {
        return YETTY_ERR(yetty_ycore_void, "no root tile");
    }

    /* Closing root? */
    if (yetty_yui_tile_id(ws->root) == tile_id) {
        yetty_yui_tile_destroy(ws->root);
        ws->root = NULL;
        return YETTY_OK_VOID();
    }

    /* Find parent split */
    parent_split = yetty_yui_tile_find_parent_split(ws->root, tile_id);
    if (!parent_split) {
        return YETTY_ERR(yetty_ycore_void, "parent split not found");
    }

    /* Determine sibling */
    if (yetty_yui_tile_id(yetty_yui_tile_split_first(parent_split)) == tile_id) {
        sibling = yetty_yui_tile_split_second(parent_split);
    } else {
        sibling = yetty_yui_tile_split_first(parent_split);
    }

    /* Parent split is root? Promote sibling to root */
    if (ws->root == parent_split) {
        /* Clear parent's children to prevent double-free */
        yetty_yui_tile_split_set_first(parent_split, NULL);
        yetty_yui_tile_split_set_second(parent_split, NULL);
        yetty_yui_tile_destroy(parent_split);

        ws->root = sibling;
        if (sibling && ws->width > 0 && ws->height > 0) {
            struct yetty_yui_rect bounds = {0, 0, ws->width, ws->height};
            return yetty_yui_tile_set_bounds(sibling, bounds);
        }
        return YETTY_OK_VOID();
    }

    /* Find grandparent and replace parent_split with sibling */
    grandparent = yetty_yui_tile_find_parent_split(ws->root, yetty_yui_tile_id(parent_split));
    if (!grandparent) {
        return YETTY_ERR(yetty_ycore_void, "grandparent not found");
    }

    /* Clear parent's children to prevent double-free */
    yetty_yui_tile_split_set_first(parent_split, NULL);
    yetty_yui_tile_split_set_second(parent_split, NULL);

    if (yetty_yui_tile_split_first(grandparent) == parent_split) {
        res = yetty_yui_tile_split_set_first(grandparent, sibling);
    } else {
        res = yetty_yui_tile_split_set_second(grandparent, sibling);
    }

    yetty_yui_tile_destroy(parent_split);

    if (YETTY_IS_ERR(res)) {
        return res;
    }

    /* Re-layout */
    if (ws->width > 0 && ws->height > 0) {
        struct yetty_yui_rect bounds = {0, 0, ws->width, ws->height};
        return yetty_yui_tile_set_bounds(ws->root, bounds);
    }

    return YETTY_OK_VOID();
}

/*=============================================================================
 * Config-based layout loading
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yui_workspace_load_layout(
    struct yetty_yui_workspace *ws, const struct yetty_yconfig_config *config,
    const struct yetty_context *yetty_ctx)
{
    struct yetty_yconfig_config *layout_config;
    struct yetty_yui_tile_ptr_result tile_res;
    //TDOO: refactor the layout creation. It should be passed recursivelly to the splits/panes
    //the pane should create the right view based on the config passed to it
    if (!ws) {
        return YETTY_ERR(yetty_ycore_void, "workspace is NULL");
    }
    if (!yetty_ctx) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ctx is NULL");
    }

    /* Get layout sub-config (optional) */
    layout_config = NULL;
    if (config) {
        layout_config = config->ops->get_node(config, "workspace/default/layout");
    }

    if (layout_config) {
        /* Create tile tree from config */
        tile_res = yetty_yui_tile_create_from_config(layout_config, yetty_ctx);
    } else {
        /* Fallback: create default single pane */
        if (!config) {
            return YETTY_ERR(yetty_ycore_void, "config is NULL");
        }

        tile_res = yetty_yui_pane_create();
        if (YETTY_IS_OK(tile_res)) {
            /* Check if VNC client mode */
            const char *vnc_client = config->ops->get_string(config, "vnc/client", NULL);
            const char *desktop_vnc_client =
                config->ops->get_string(config, "vnc/desktop-client", NULL);
            ydebug("workspace: vnc_client='%s' desktop_vnc_client='%s'",
                   vnc_client ? vnc_client : "(null)",
                   desktop_vnc_client ? desktop_vnc_client : "(null)");

            if (desktop_vnc_client && strlen(desktop_vnc_client) > 0) {
                /* Parse host:port */
                char host[256] = {0};
                uint16_t port = 5900;
                const char *colon = strchr(desktop_vnc_client, ':');
                if (colon) {
                    size_t host_len = (size_t)(colon - desktop_vnc_client);
                    if (host_len >= sizeof(host)) {
                        host_len = sizeof(host) - 1;
                    }
                    memcpy(host, desktop_vnc_client, host_len);
                    port = (uint16_t)atoi(colon + 1);
                } else {
                    strncpy(host, desktop_vnc_client, sizeof(host) - 1);
                }

                const char *password = config->ops->get_string(config, "vnc/ydvnc-password", NULL);
                if (!password || !password[0]) {
                    password = getenv("YDVNC_PASSWORD");
                }
                struct yetty_ydvnc_viewer_ptr_result dv_res =
                    yetty_ydvnc_viewer_create(host, port, password, yetty_ctx);
                if (YETTY_IS_ERR(dv_res)) {
                    yetty_yui_tile_destroy(tile_res.value);
                    return YETTY_ERR(yetty_ycore_void, "ydvnc viewer create failed", dv_res);
                }

                yetty_yui_tile_pane_push_view(tile_res.value,
                                              yetty_ydvnc_viewer_as_view(dv_res.value));
                yetty_yui_tile_pane_set_focused(tile_res.value, 1);
            } else if (vnc_client && strlen(vnc_client) > 0) {
                /* Parse host:port */
                char host[256] = {0};
                uint16_t port = 5900;
                const char *colon = strchr(vnc_client, ':');
                if (colon) {
                    size_t host_len = (size_t)(colon - vnc_client);
                    if (host_len >= sizeof(host)) {
                        host_len = sizeof(host) - 1;
                    }
                    memcpy(host, vnc_client, host_len);
                    port = (uint16_t)atoi(colon + 1);
                } else {
                    strncpy(host, vnc_client, sizeof(host) - 1);
                }

                /* Create VNC viewer */
                struct yetty_vnc_viewer_ptr_result vnc_res =
                    yetty_yvnc_viewer_create(host, port, yetty_ctx);
                if (YETTY_IS_ERR(vnc_res)) {
                    yetty_yui_tile_destroy(tile_res.value);
                    return YETTY_ERR(yetty_ycore_void, vnc_res.error.msg);
                }

                yetty_yui_tile_pane_push_view(tile_res.value,
                                              yetty_yvnc_viewer_as_view(vnc_res.value));
                yetty_yui_tile_pane_set_focused(tile_res.value, 1);
            } else {
                /* Create terminal */
                struct yetty_yterm_terminal_result term_res;
                struct yetty_ycore_grid_size grid_size = {.rows = 24, .cols = 80};

                term_res = yetty_yterm_terminal_create(grid_size, yetty_ctx);
                if (YETTY_IS_ERR(term_res)) {
                    yetty_yui_tile_destroy(tile_res.value);
                    return YETTY_ERR(yetty_ycore_void, term_res.error.msg);
                }

                yetty_yui_tile_pane_push_view(tile_res.value,
                                              yetty_yterm_terminal_as_view(term_res.value));
                yetty_yui_tile_pane_set_focused(tile_res.value, 1);
            }
        }
    }

    if (YETTY_IS_ERR(tile_res)) {
        return YETTY_ERR(yetty_ycore_void, tile_res.error.msg);
    }

    /* Set as root */
    struct yetty_ycore_void_result res = yetty_yui_workspace_set_root(ws, tile_res.value);
    if (YETTY_IS_ERR(res)) {
        return res;
    }

    /* Focus the first pane in the tree */
    struct yetty_yui_tile *first_pane = yetty_yui_tile_find_first_pane(ws->root);
    if (first_pane) {
        yetty_yui_tile_pane_set_focused(first_pane, 1);
    }

    return YETTY_OK_VOID();
}

/*=============================================================================
 * Event handling
 *===========================================================================*/

struct yetty_ycore_int_result yetty_yui_workspace_on_event(struct yetty_yui_workspace *ws,
                                                           const struct yetty_yui_event *event)
{
    struct yetty_yui_tile *focused_pane;
    struct yetty_yui_tile *clicked_pane;

    if (!ws) {
        return YETTY_ERR(yetty_ycore_int, "workspace is NULL");
    }
    if (!ws->root) {
        return YETTY_OK(yetty_ycore_int, 0);
    }

    /* Handle mouse down - update focus */
    if (event->type == YETTY_YCORE_MOUSE_DOWN) {
        ydebug("workspace: MOUSE_DOWN at (%.1f, %.1f)", event->mouse.x, event->mouse.y);
        clicked_pane = yetty_yui_tile_find_pane_at(ws->root, event->mouse.x, event->mouse.y);
        ydebug("workspace: clicked_pane=%p", (void *)clicked_pane);
        if (clicked_pane) {
            struct yetty_yui_rect b = yetty_yui_tile_bounds(clicked_pane);
            ydebug("workspace: pane bounds=(%.1f,%.1f,%.1f,%.1f) focused=%d", b.x, b.y, b.w, b.h,
                   yetty_yui_tile_pane_focused(clicked_pane));
        }
        if (clicked_pane && !yetty_yui_tile_pane_focused(clicked_pane)) {
            ydebug("workspace: switching focus to pane %p", (void *)clicked_pane);
            /* Clear focus from all panes, set on clicked */
            yetty_yui_tile_clear_focus(ws->root);
            yetty_yui_tile_pane_set_focused(clicked_pane, 1);
        }
        /* Pass event to clicked pane */
        if (clicked_pane) {
            return yetty_yui_tile_on_event(clicked_pane, event);
        }
        return YETTY_OK(yetty_ycore_int, 0);
    }

    /* Keyboard events go only to focused pane */
    if (event->type == YETTY_YCORE_KEY_DOWN || event->type == YETTY_YCORE_KEY_UP ||
        event->type == YETTY_YCORE_CHAR) {
        focused_pane = yetty_yui_tile_find_focused_pane(ws->root);
        ydebug("workspace: keyboard event type=%d focused_pane=%p", event->type,
               (void *)focused_pane);
        if (focused_pane) {
            return yetty_yui_tile_on_event(focused_pane, event);
        }
        return YETTY_OK(yetty_ycore_int, 0);
    }

    /* Other events (resize, etc.) pass through to tile tree */
    return yetty_yui_tile_on_event(ws->root, event);
}
