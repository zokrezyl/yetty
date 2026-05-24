#include <yetty/yui/tile.h>
#include <yetty/yui-core/view.h>
#include <yetty/yconfig/config.h>
#include <yetty/yevent/event.h>
#include <yetty/ycore/util.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yetty/yetty.h>
#include <yetty/yframework/yframework.h>
#include <yetty/yterm/terminal.h>
#include <yetty/yterm/background-layer.h>
#include <yetty/yvnc/vnc-viewer.h>
#include <yetty/ydvnc/ydvnc-viewer.h>
#include <yetty/ytrace/ytrace.h>
#include <stdlib.h>
#include <string.h>

/*=============================================================================
 * Internal tile structures
 *===========================================================================*/

struct yetty_yui_tile {
    const struct yetty_yui_tile_ops *ops;
    yetty_ycore_object_id id;
    enum yetty_yui_tile_type type;
    struct yetty_yui_rect bounds;
    struct yetty_yui_tile *parent;
};

struct yetty_yui_tile_ops {
    struct yetty_ycore_void_result (*destroy)(struct yetty_yui_tile *self);
    struct yetty_ycore_void_result (*render)(struct yetty_yui_tile *self,
                                             struct yetty_ydraw_target *render_target,
                                             int force_redraw);
    struct yetty_ycore_void_result (*set_bounds)(struct yetty_yui_tile *self,
                                                 struct yetty_yui_rect bounds);
    struct yetty_ycore_int_result (*on_event)(struct yetty_yui_tile *self,
                                              const struct yetty_yui_event *event);
};

struct yetty_yui_split {
    struct yetty_yui_tile base;
    enum yetty_yui_orientation orientation;
    float ratio;
    struct yetty_yui_tile *first;
    struct yetty_yui_tile *second;
};

struct yetty_yui_pane {
    struct yetty_yui_tile base;
    struct yetty_yui_view **views;
    size_t view_count;
    size_t view_capacity;
    int focused;

    /* Per-pane background — opaque RGBA fill rendered before the view's
     * own layers. Owned by the pane; created lazily by
     * tile_create_from_config when the pane is built from YAML. NULL means
     * "no background" (other panes' previous-frame pixels show through —
     * effectively the pre-bg-layer behaviour). */
    struct yetty_yrender_terminal_layer *background_layer;
};

/*=============================================================================
 * Split implementation
 *===========================================================================*/

static struct yetty_ycore_void_result split_destroy(struct yetty_yui_tile *self)
{
    struct yetty_yui_split *split = (struct yetty_yui_split *)self;
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();

    if (split->first) {
        struct yetty_ycore_void_result r = yetty_yui_tile_destroy(split->first);
        if (YETTY_IS_ERR(r)) {
            first_err = r;
        }
    }
    if (split->second) {
        struct yetty_ycore_void_result r = yetty_yui_tile_destroy(split->second);
        if (YETTY_IS_ERR(r)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = r;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
    }

    free(split);

    if (YETTY_IS_ERR(first_err)) {
        return YETTY_ERR(yetty_ycore_void, "split_destroy: child failed", first_err);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result split_render(struct yetty_yui_tile *self,
                                                   struct yetty_ydraw_target *render_target,
                                                   int force_redraw)
{
    struct yetty_yui_split *split = (struct yetty_yui_split *)self;
    struct yetty_ycore_void_result res;

    if (split->first) {
        res = yetty_yui_tile_render(split->first, render_target, force_redraw);
        if (YETTY_IS_ERR(res)) {
            return res;
        }
    }
    if (split->second) {
        res = yetty_yui_tile_render(split->second, render_target, force_redraw);
        if (YETTY_IS_ERR(res)) {
            return res;
        }
    }

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result split_set_bounds(struct yetty_yui_tile *self,
                                                       struct yetty_yui_rect bounds)
{
    struct yetty_yui_split *split = (struct yetty_yui_split *)self;
    struct yetty_yui_rect first_bounds, second_bounds;

    split->base.bounds = bounds;

    if (split->orientation == YETTY_YUI_HORIZONTAL) {
        float first_h = bounds.h * split->ratio;

        first_bounds = (struct yetty_yui_rect){bounds.x, bounds.y, bounds.w, first_h};
        second_bounds =
            (struct yetty_yui_rect){bounds.x, bounds.y + first_h, bounds.w, bounds.h - first_h};
    } else {
        float first_w = bounds.w * split->ratio;

        first_bounds = (struct yetty_yui_rect){bounds.x, bounds.y, first_w, bounds.h};
        second_bounds =
            (struct yetty_yui_rect){bounds.x + first_w, bounds.y, bounds.w - first_w, bounds.h};
    }

    if (split->first) {
        yetty_yui_tile_set_bounds(split->first, first_bounds);
    }
    if (split->second) {
        yetty_yui_tile_set_bounds(split->second, second_bounds);
    }

    return YETTY_OK_VOID();
}

static struct yetty_ycore_int_result split_on_event(struct yetty_yui_tile *self,
                                                    const struct yetty_yui_event *event)
{
    struct yetty_yui_split *split = (struct yetty_yui_split *)self;

    if (event->type == YETTY_YCORE_RESIZE) {
        /* Calculate child bounds based on orientation and ratio */
        float w = event->resize.width;
        float h = event->resize.height;
        struct yetty_yui_event first_event = *event;
        struct yetty_yui_event second_event = *event;

        if (split->orientation == YETTY_YUI_HORIZONTAL) {
            float first_h = h * split->ratio;
            first_event.resize.height = first_h;
            second_event.resize.height = h - first_h;
        } else {
            float first_w = w * split->ratio;
            first_event.resize.width = first_w;
            second_event.resize.width = w - first_w;
        }

        /* Pass to both children with their respective sizes */
        if (split->first) {
            yetty_yui_tile_on_event(split->first, &first_event);
        }
        if (split->second) {
            yetty_yui_tile_on_event(split->second, &second_event);
        }

        return YETTY_OK(yetty_ycore_int, 1);
    }

    /* For other events, pass to focused child */
    if (split->first) {
        struct yetty_ycore_int_result res = yetty_yui_tile_on_event(split->first, event);
        if (YETTY_IS_OK(res) && res.value) {
            return res;
        }
    }
    if (split->second) {
        struct yetty_ycore_int_result res = yetty_yui_tile_on_event(split->second, event);
        if (YETTY_IS_OK(res) && res.value) {
            return res;
        }
    }

    return YETTY_OK(yetty_ycore_int, 0);
}

static const struct yetty_yui_tile_ops split_ops = {
    .destroy = split_destroy,
    .render = split_render,
    .set_bounds = split_set_bounds,
    .on_event = split_on_event,
};

struct yetty_yui_tile_ptr_result yetty_yui_split_create_with_id(
    yetty_ycore_object_id id, enum yetty_yui_orientation orientation)
{
    struct yetty_yui_split *split;

    split = calloc(1, sizeof(struct yetty_yui_split));
    if (!split) {
        return YETTY_ERR(yetty_yui_tile_ptr, "allocation failed");
    }

    split->base.ops = &split_ops;
    split->base.id = id;
    split->base.type = YETTY_YUI_TILE_SPLIT;
    split->orientation = orientation;
    split->ratio = 0.5f;

    return YETTY_OK(yetty_yui_tile_ptr, &split->base);
}

struct yetty_yui_tile_ptr_result yetty_yui_split_create(enum yetty_yui_orientation orientation)
{
    return yetty_yui_split_create_with_id(yetty_ycore_next_object_id(), orientation);
}

/*=============================================================================
 * Pane implementation
 *===========================================================================*/

static struct yetty_ycore_void_result pane_destroy(struct yetty_yui_tile *self)
{
    struct yetty_yui_pane *pane = (struct yetty_yui_pane *)self;
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();

    if (pane->background_layer && pane->background_layer->ops &&
        pane->background_layer->ops->destroy) {
        struct yetty_ycore_void_result r =
            pane->background_layer->ops->destroy(pane->background_layer);
        if (YETTY_IS_ERR(r)) {
            first_err = r;
        }
        pane->background_layer = NULL;
    }

    for (size_t i = 0; i < pane->view_count; i++) {
        if (pane->views[i]) {
            struct yetty_ycore_void_result r = yetty_yui_view_destroy(pane->views[i]);
            if (YETTY_IS_ERR(r)) {
                if (YETTY_IS_OK(first_err)) {
                    first_err = r;
                } else {
                    yetty_ycore_error_destroy(r.error);
                }
            }
        }
    }

    free(pane->views);
    free(pane);

    if (YETTY_IS_ERR(first_err)) {
        return YETTY_ERR(yetty_ycore_void, "pane_destroy: view destroy failed", first_err);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result pane_render(struct yetty_yui_tile *self,
                                                  struct yetty_ydraw_target *render_target,
                                                  int force_redraw)
{
    struct yetty_yui_pane *pane = (struct yetty_yui_pane *)self;

    if (pane->view_count > 0 && pane->views[pane->view_count - 1]) {
        /* Mutate render_target->viewport down to this pane's bounds for
         * per-layer scissor, then restore on exit. The target's viewport
         * is *also* read by present()/blend() to mean "full surface
         * dimensions", so leaving it pointing at the last pane's bounds
         * would make the X11/VNC sink only blit/diff a single pane-sized
         * region (multi-pane symptom: only one pane's pixels survive). */
        struct yetty_yrender_viewport saved_vp = render_target->viewport;
        struct yetty_yui_rect bounds = pane->base.bounds;
        render_target->viewport = (struct yetty_yrender_viewport){
            .x = bounds.x, .y = bounds.y, .w = bounds.w, .h = bounds.h};
        ydebug("pane_render: bounds=(%.1f,%.1f,%.1f,%.1f) -> render_target viewport", bounds.x,
               bounds.y, bounds.w, bounds.h);

        /* Pane background — opaque RGBA fill across the pane viewport,
         * rendered before the view. Provides the per-pane wipe so the
         * view's upper layers (alpha<1) don't ghost the previous frame. */
        struct yetty_ycore_void_result res = YETTY_OK_VOID();
        if (pane->background_layer && pane->background_layer->ops &&
            pane->background_layer->ops->render) {
            /* yui's pane-level draw: stand-alone background pass, no
             * cascade in flight here. force=force_redraw so the global
             * yui-dirty signal also wipes this pane's background fill,
             * covering whatever the yui scene-canvas (cards, dialogs)
             * used to paint inside the pane's region. Drop the int
             * return (only success/failure matters at this site). */
            struct yetty_ycore_int_result rr = pane->background_layer->ops->render(
                pane->background_layer, render_target, force_redraw);
            if (YETTY_IS_ERR(rr)) {
                res = YETTY_ERR(yetty_ycore_void, "pane background render", rr);
            }
        }
        if (YETTY_IS_OK(res)) {
            res = yetty_yui_view_render(pane->views[pane->view_count - 1], render_target,
                                        force_redraw);
        }

        render_target->viewport = saved_vp;
        return res;
    }

    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result pane_set_bounds(struct yetty_yui_tile *self,
                                                      struct yetty_yui_rect bounds)
{
    struct yetty_yui_pane *pane = (struct yetty_yui_pane *)self;

    pane->base.bounds = bounds;

    /* Propagate to active view */
    if (pane->view_count > 0 && pane->views[pane->view_count - 1]) {
        yetty_yui_view_set_bounds(pane->views[pane->view_count - 1], bounds);
    }

    return YETTY_OK_VOID();
}

static struct yetty_ycore_int_result pane_on_event(struct yetty_yui_tile *self,
                                                   const struct yetty_yui_event *event)
{
    struct yetty_yui_pane *pane = (struct yetty_yui_pane *)self;

    /* Pass to active view */
    if (pane->view_count > 0 && pane->views[pane->view_count - 1]) {
        return yetty_yui_view_on_event(pane->views[pane->view_count - 1], event);
    }

    return YETTY_OK(yetty_ycore_int, 0);
}

static const struct yetty_yui_tile_ops pane_ops = {
    .destroy = pane_destroy,
    .render = pane_render,
    .set_bounds = pane_set_bounds,
    .on_event = pane_on_event,
};

struct yetty_yui_tile_ptr_result yetty_yui_pane_create_with_id(yetty_ycore_object_id id)
{
    struct yetty_yui_pane *pane;

    pane = calloc(1, sizeof(struct yetty_yui_pane));
    if (!pane) {
        return YETTY_ERR(yetty_yui_tile_ptr, "allocation failed");
    }

    pane->base.ops = &pane_ops;
    pane->base.id = id;
    pane->base.type = YETTY_YUI_TILE_PANE;

    return YETTY_OK(yetty_yui_tile_ptr, &pane->base);
}

struct yetty_yui_tile_ptr_result yetty_yui_pane_create(void)
{
    return yetty_yui_pane_create_with_id(yetty_ycore_next_object_id());
}

/*=============================================================================
 * Tile public API
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yui_tile_destroy(struct yetty_yui_tile *tile)
{
    if (!tile) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yui_tile_destroy: NULL tile");
    }
    if (!tile->ops || !tile->ops->destroy) {
        return YETTY_ERR(yetty_ycore_void, "yetty_yui_tile_destroy: destroy not implemented");
    }
    return tile->ops->destroy(tile);
}

struct yetty_ycore_void_result yetty_yui_tile_render(struct yetty_yui_tile *tile,
                                                     struct yetty_ydraw_target *render_target,
                                                     int force_redraw)
{
    if (!tile) {
        return YETTY_ERR(yetty_ycore_void, "tile is NULL");
    }
    if (!tile->ops || !tile->ops->render) {
        return YETTY_ERR(yetty_ycore_void, "render not implemented");
    }
    return tile->ops->render(tile, render_target, force_redraw);
}

struct yetty_ycore_void_result yetty_yui_tile_set_bounds(struct yetty_yui_tile *tile,
                                                         struct yetty_yui_rect bounds)
{
    if (!tile) {
        return YETTY_ERR(yetty_ycore_void, "tile is NULL");
    }
    if (!tile->ops || !tile->ops->set_bounds) {
        return YETTY_ERR(yetty_ycore_void, "set_bounds not implemented");
    }
    return tile->ops->set_bounds(tile, bounds);
}

struct yetty_ycore_int_result yetty_yui_tile_on_event(struct yetty_yui_tile *tile,
                                                      const struct yetty_yui_event *event)
{
    if (!tile) {
        return YETTY_ERR(yetty_ycore_int, "tile is NULL");
    }
    if (!tile->ops || !tile->ops->on_event) {
        return YETTY_OK(yetty_ycore_int, 0);
    }
    return tile->ops->on_event(tile, event);
}

yetty_ycore_object_id yetty_yui_tile_id(const struct yetty_yui_tile *tile)
{
    return tile ? tile->id : YETTY_YCORE_OBJECT_ID_NONE;
}

enum yetty_yui_tile_type yetty_yui_tile_type(const struct yetty_yui_tile *tile)
{
    return tile ? tile->type : YETTY_YUI_TILE_PANE;
}

struct yetty_yui_rect yetty_yui_tile_bounds(const struct yetty_yui_tile *tile)
{
    if (!tile) {
        return (struct yetty_yui_rect){0, 0, 0, 0};
    }
    return tile->bounds;
}

/*=============================================================================
 * Split public API
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yui_tile_split_set_first(struct yetty_yui_tile *tile,
                                                              struct yetty_yui_tile *child)
{
    struct yetty_yui_split *split;

    if (!tile || tile->type != YETTY_YUI_TILE_SPLIT) {
        return YETTY_ERR(yetty_ycore_void, "not a split");
    }

    split = (struct yetty_yui_split *)tile;
    split->first = child;
    if (child) {
        child->parent = tile;
    }

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_tile_split_set_second(struct yetty_yui_tile *tile,
                                                               struct yetty_yui_tile *child)
{
    struct yetty_yui_split *split;

    if (!tile || tile->type != YETTY_YUI_TILE_SPLIT) {
        return YETTY_ERR(yetty_ycore_void, "not a split");
    }

    split = (struct yetty_yui_split *)tile;
    split->second = child;
    if (child) {
        child->parent = tile;
    }

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_tile_split_set_ratio(struct yetty_yui_tile *tile,
                                                              float ratio)
{
    struct yetty_yui_split *split;

    if (!tile || tile->type != YETTY_YUI_TILE_SPLIT) {
        return YETTY_ERR(yetty_ycore_void, "not a split");
    }
    if (ratio < 0.0f || ratio > 1.0f) {
        return YETTY_ERR(yetty_ycore_void, "ratio must be 0-1");
    }

    split = (struct yetty_yui_split *)tile;
    split->ratio = ratio;

    return YETTY_OK_VOID();
}

struct yetty_yui_tile *yetty_yui_tile_split_first(const struct yetty_yui_tile *tile)
{
    if (!tile || tile->type != YETTY_YUI_TILE_SPLIT) {
        return NULL;
    }
    return ((struct yetty_yui_split *)tile)->first;
}

struct yetty_yui_tile *yetty_yui_tile_split_second(const struct yetty_yui_tile *tile)
{
    if (!tile || tile->type != YETTY_YUI_TILE_SPLIT) {
        return NULL;
    }
    return ((struct yetty_yui_split *)tile)->second;
}

float yetty_yui_tile_split_ratio(const struct yetty_yui_tile *tile)
{
    if (!tile || tile->type != YETTY_YUI_TILE_SPLIT) {
        return 0.5f;
    }
    return ((struct yetty_yui_split *)tile)->ratio;
}

enum yetty_yui_orientation yetty_yui_tile_split_orientation(const struct yetty_yui_tile *tile)
{
    if (!tile || tile->type != YETTY_YUI_TILE_SPLIT) {
        return YETTY_YUI_HORIZONTAL;
    }
    return ((struct yetty_yui_split *)tile)->orientation;
}

/*=============================================================================
 * Pane public API
 *===========================================================================*/

struct yetty_ycore_void_result yetty_yui_tile_pane_push_view(struct yetty_yui_tile *tile,
                                                             struct yetty_yui_view *view)
{
    struct yetty_yui_pane *pane;

    if (!tile || tile->type != YETTY_YUI_TILE_PANE) {
        return YETTY_ERR(yetty_ycore_void, "not a pane");
    }

    pane = (struct yetty_yui_pane *)tile;

    if (pane->view_count >= pane->view_capacity) {
        size_t new_cap = pane->view_capacity ? pane->view_capacity * 2 : 4;
        struct yetty_yui_view **new_views;

        new_views = realloc(pane->views, new_cap * sizeof(struct yetty_yui_view *));
        if (!new_views) {
            return YETTY_ERR(yetty_ycore_void, "allocation failed");
        }

        pane->views = new_views;
        pane->view_capacity = new_cap;
    }

    pane->views[pane->view_count++] = view;

    /* Set bounds on new view */
    if (view) {
        yetty_yui_view_set_bounds(view, pane->base.bounds);
    }

    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yui_tile_pane_pop_view(struct yetty_yui_tile *tile)
{
    struct yetty_yui_pane *pane;

    if (!tile || tile->type != YETTY_YUI_TILE_PANE) {
        return YETTY_ERR(yetty_ycore_void, "not a pane");
    }

    pane = (struct yetty_yui_pane *)tile;

    if (pane->view_count == 0) {
        return YETTY_ERR(yetty_ycore_void, "no views to pop");
    }

    pane->view_count--;

    return YETTY_OK_VOID();
}

struct yetty_yui_view *yetty_yui_tile_pane_active_view(const struct yetty_yui_tile *tile)
{
    const struct yetty_yui_pane *pane;

    if (!tile || tile->type != YETTY_YUI_TILE_PANE) {
        return NULL;
    }

    pane = (const struct yetty_yui_pane *)tile;
    if (pane->view_count == 0) {
        return NULL;
    }

    return pane->views[pane->view_count - 1];
}

size_t yetty_yui_tile_pane_view_count(const struct yetty_yui_tile *tile)
{
    if (!tile || tile->type != YETTY_YUI_TILE_PANE) {
        return 0;
    }
    return ((const struct yetty_yui_pane *)tile)->view_count;
}

int yetty_yui_tile_pane_has_view(const struct yetty_yui_tile *tile, yetty_ycore_object_id view_id)
{
    const struct yetty_yui_pane *pane;

    if (!tile || tile->type != YETTY_YUI_TILE_PANE) {
        return 0;
    }

    pane = (const struct yetty_yui_pane *)tile;
    for (size_t i = 0; i < pane->view_count; i++) {
        if (pane->views[i] && yetty_yui_view_id(pane->views[i]) == view_id) {
            return 1;
        }
    }

    return 0;
}

int yetty_yui_tile_pane_focused(const struct yetty_yui_tile *tile)
{
    if (!tile || tile->type != YETTY_YUI_TILE_PANE) {
        return 0;
    }
    return ((const struct yetty_yui_pane *)tile)->focused;
}

void yetty_yui_tile_pane_set_focused(struct yetty_yui_tile *tile, int focused)
{
    if (!tile || tile->type != YETTY_YUI_TILE_PANE) {
        return;
    }
    ((struct yetty_yui_pane *)tile)->focused = focused;
}

/*=============================================================================
 * Tree helpers
 *===========================================================================*/

struct yetty_yui_tile *yetty_yui_tile_find_by_id(struct yetty_yui_tile *root,
                                                 yetty_ycore_object_id id)
{
    struct yetty_yui_tile *found;

    if (!root) {
        return NULL;
    }
    if (root->id == id) {
        return root;
    }

    if (root->type == YETTY_YUI_TILE_SPLIT) {
        struct yetty_yui_split *split = (struct yetty_yui_split *)root;

        found = yetty_yui_tile_find_by_id(split->first, id);
        if (found) {
            return found;
        }
        return yetty_yui_tile_find_by_id(split->second, id);
    }

    return NULL;
}

struct yetty_yui_tile *yetty_yui_tile_find_parent_split(struct yetty_yui_tile *root,
                                                        yetty_ycore_object_id target_id)
{
    struct yetty_yui_split *split;
    struct yetty_yui_tile *found;

    if (!root || root->type != YETTY_YUI_TILE_SPLIT) {
        return NULL;
    }

    split = (struct yetty_yui_split *)root;

    if ((split->first && split->first->id == target_id) ||
        (split->second && split->second->id == target_id)) {
        return root;
    }

    found = yetty_yui_tile_find_parent_split(split->first, target_id);
    if (found) {
        return found;
    }

    return yetty_yui_tile_find_parent_split(split->second, target_id);
}

struct yetty_yui_tile *yetty_yui_tile_find_focused_pane(struct yetty_yui_tile *root)
{
    struct yetty_yui_split *split;
    struct yetty_yui_tile *found;

    if (!root) {
        return NULL;
    }

    if (root->type == YETTY_YUI_TILE_PANE) {
        if (((struct yetty_yui_pane *)root)->focused) {
            return root;
        }
        return NULL;
    }

    split = (struct yetty_yui_split *)root;
    found = yetty_yui_tile_find_focused_pane(split->first);
    if (found) {
        return found;
    }

    return yetty_yui_tile_find_focused_pane(split->second);
}

static int point_in_rect(float x, float y, struct yetty_yui_rect r)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

struct yetty_yui_tile *yetty_yui_tile_find_pane_at(struct yetty_yui_tile *root, float x, float y)
{
    struct yetty_yui_split *split;
    struct yetty_yui_tile *found;

    if (!root) {
        return NULL;
    }

    ydebug("find_pane_at: tile type=%d bounds=(%.1f,%.1f,%.1f,%.1f) point=(%.1f,%.1f)", root->type,
           root->bounds.x, root->bounds.y, root->bounds.w, root->bounds.h, x, y);

    /* Check if point is in this tile's bounds */
    if (!point_in_rect(x, y, root->bounds)) {
        ydebug("find_pane_at: point NOT in bounds");
        return NULL;
    }

    if (root->type == YETTY_YUI_TILE_PANE) {
        ydebug("find_pane_at: found pane at (%.1f,%.1f)", x, y);
        return root;
    }

    /* Split - recurse into children */
    split = (struct yetty_yui_split *)root;
    found = yetty_yui_tile_find_pane_at(split->first, x, y);
    if (found) {
        return found;
    }

    return yetty_yui_tile_find_pane_at(split->second, x, y);
}

struct yetty_yui_tile *yetty_yui_tile_find_first_pane(struct yetty_yui_tile *root)
{
    struct yetty_yui_split *split;
    struct yetty_yui_tile *found;

    if (!root) {
        return NULL;
    }

    if (root->type == YETTY_YUI_TILE_PANE) {
        return root;
    }

    split = (struct yetty_yui_split *)root;
    found = yetty_yui_tile_find_first_pane(split->first);
    if (found) {
        return found;
    }

    return yetty_yui_tile_find_first_pane(split->second);
}

void yetty_yui_tile_clear_focus(struct yetty_yui_tile *root)
{
    struct yetty_yui_split *split;

    if (!root) {
        return;
    }

    if (root->type == YETTY_YUI_TILE_PANE) {
        yetty_yui_tile_pane_set_focused(root, 0);
        return;
    }

    split = (struct yetty_yui_split *)root;
    yetty_yui_tile_clear_focus(split->first);
    yetty_yui_tile_clear_focus(split->second);
}

/*=============================================================================
 * Config-based creation
 *===========================================================================*/

struct yetty_yui_tile_ptr_result yetty_yui_tile_create_from_config(
    const struct yetty_yconfig_config *config, const struct yetty_context *yetty_ctx)
{
    const char *type;
    struct yetty_yui_tile_ptr_result res;

    if (!config) {
        return YETTY_ERR(yetty_yui_tile_ptr, "config is NULL");
    }
    if (!yetty_ctx) {
        return YETTY_ERR(yetty_yui_tile_ptr, "yetty_ctx is NULL");
    }

    type = config->ops->get_string(config, "type", "pane");

    if (strcmp(type, "split") == 0) {
        const char *orient_str;
        enum yetty_yui_orientation orientation;
        float ratio;
        struct yetty_yconfig_config *first_config;
        struct yetty_yconfig_config *second_config;
        struct yetty_yui_tile_ptr_result first_res, second_res;

        /* Parse orientation */
        orient_str = config->ops->get_string(config, "orientation", "horizontal");
        if (strcmp(orient_str, "vertical") == 0) {
            orientation = YETTY_YUI_VERTICAL;
        } else {
            orientation = YETTY_YUI_HORIZONTAL;
        }

        /* Create split */
        res = yetty_yui_split_create(orientation);
        if (YETTY_IS_ERR(res)) {
            return res;
        }

        /* Set ratio */
        ratio = (float)config->ops->get_int(config, "ratio", 50) / 100.0f;
        if (ratio < 0.1f) {
            ratio = 0.1f;
        }
        if (ratio > 0.9f) {
            ratio = 0.9f;
        }
        yetty_yui_tile_split_set_ratio(res.value, ratio);

        /* Create first child */
        first_config = config->ops->get_node(config, "first");
        if (first_config) {
            first_res = yetty_yui_tile_create_from_config(first_config, yetty_ctx);
            if (YETTY_IS_ERR(first_res)) {
                yetty_yui_tile_destroy(res.value);
                return first_res;
            }
            yetty_yui_tile_split_set_first(res.value, first_res.value);
        }

        /* Create second child */
        second_config = config->ops->get_node(config, "second");
        if (second_config) {
            second_res = yetty_yui_tile_create_from_config(second_config, yetty_ctx);
            if (YETTY_IS_ERR(second_res)) {
                yetty_yui_tile_destroy(res.value);
                return second_res;
            }
            yetty_yui_tile_split_set_second(res.value, second_res.value);
        }

        return res;
    }

    /* Default: pane */
    res = yetty_yui_pane_create();
    if (YETTY_IS_ERR(res)) {
        return res;
    }

    /* Optional pane background — sibling of `view` in the layout YAML, e.g.
     *   background:
     *     color: "#101020"
     * Accepted formats: #RGB / #RGBA / #RRGGBB / #RRGGBBAA. */
    {
        struct yetty_yconfig_config *bg_node = config->ops->get_node(config, "background");
        if (bg_node) {
            const char *color_str = bg_node->ops->get_string(bg_node, "color", NULL);
            uint32_t packed = 0;
            if (color_str && yetty_ycore_parse_hex_color(color_str, &packed)) {
                float rgba[4] = {
                    (float)(packed & 0xFFu) / 255.0f,
                    (float)((packed >> 8) & 0xFFu) / 255.0f,
                    (float)((packed >> 16) & 0xFFu) / 255.0f,
                    (float)((packed >> 24) & 0xFFu) / 255.0f,
                };
                struct yetty_yterm_terminal_layer_result bg_res =
                    yetty_yterm_background_layer_create(yetty_ctx, rgba);
                if (YETTY_IS_OK(bg_res)) {
                    ((struct yetty_yui_pane *)res.value)->background_layer = bg_res.value;
                    ydebug("tile: pane background '%s' -> (%.2f, %.2f, %.2f, %.2f)", color_str,
                           rgba[0], rgba[1], rgba[2], rgba[3]);
                } else {
                    ywarn("tile: pane background create failed: %s — pane will draw "
                          "without an opaque base",
                          bg_res.error.msg);
                }
            } else if (color_str) {
                ywarn("tile: pane background color '%s' unparseable "
                      "(expected #RGB / #RGBA / #RRGGBB / #RRGGBBAA), "
                      "no pane background will be drawn",
                      color_str);
            }
        }
    }

    /* Create view based on config or vnc/client / vnc/desktop-client override */
    {
        const char *vnc_client = NULL;
        const char *desktop_vnc_client = NULL;
        struct yetty_yconfig_config *app_config = yetty_ctx->runtime->config;

        if (app_config) {
            vnc_client = app_config->ops->get_string(app_config, "vnc/client", NULL);
            desktop_vnc_client =
                app_config->ops->get_string(app_config, "vnc/desktop-client", NULL);
        }

        if (desktop_vnc_client && strlen(desktop_vnc_client) > 0) {
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

            const char *password =
                app_config->ops->get_string(app_config, "vnc/ydvnc-password", NULL);
            if (!password || !password[0]) {
                password = getenv("YDVNC_PASSWORD");
            }
            struct yetty_ydvnc_viewer_ptr_result dv_res =
                yetty_ydvnc_viewer_create(host, port, password, yetty_ctx);
            if (YETTY_IS_ERR(dv_res)) {
                yetty_yui_tile_destroy(res.value);
                return YETTY_ERR(yetty_yui_tile_ptr, "ydvnc viewer create failed", dv_res);
            }

            yetty_yui_tile_pane_push_view(res.value, yetty_ydvnc_viewer_as_view(dv_res.value));
        } else if (vnc_client && strlen(vnc_client) > 0) {
            /* VNC client mode: create VNC viewer */
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

            struct yetty_vnc_viewer_ptr_result vnc_res =
                yetty_yvnc_viewer_create(host, port, yetty_ctx);
            if (YETTY_IS_ERR(vnc_res)) {
                yetty_yui_tile_destroy(res.value);
                return YETTY_ERR(yetty_yui_tile_ptr, vnc_res.error.msg);
            }

            yetty_yui_tile_pane_push_view(res.value, yetty_yvnc_viewer_as_view(vnc_res.value));
        } else {
            /* Normal mode: create terminal */
            const char *view_type;
            struct yetty_yterm_terminal_result term_res;
            struct yetty_ycore_grid_size grid_size = {.rows = 24, .cols = 80};

            view_type = config->ops->get_string(config, "view", "terminal");

            if (strcmp(view_type, "terminal") == 0) {
                term_res = yetty_yterm_terminal_create(grid_size, yetty_ctx);
                if (YETTY_IS_ERR(term_res)) {
                    yetty_yui_tile_destroy(res.value);
                    return YETTY_ERR(yetty_yui_tile_ptr, term_res.error.msg);
                }

                yetty_yui_tile_pane_push_view(res.value,
                                              yetty_yterm_terminal_as_view(term_res.value));
            }
        }
    }

    /* Focus is set by workspace after layout is fully built */
    return res;
}
