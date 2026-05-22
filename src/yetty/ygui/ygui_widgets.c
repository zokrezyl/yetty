/*
 * ygui_widgets.c - Widget implementations
 */

#include "ygui_internal.h"
#include <yetty/ytrace/ytrace.h>
#include <stdio.h>
#include <stdlib.h>

/*=============================================================================
 * Widget Base Functions
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_widget_alloc(struct yetty_ygui_engine *engine,
                                                         ygui_widget_type_t type, const char *id)
{
    struct yetty_ygui_widget *w =
        (struct yetty_ygui_widget *)calloc(1, sizeof(struct yetty_ygui_widget));
    if (!w) {
        yetty_ygui_set_error("Failed to allocate widget");
        return NULL;
    }

    w->id = ygui_strdup(id);
    w->type = type;
    w->engine = engine;
    w->flags = YETTY_YGUI_FLAG_VISIBLE;
    w->bg_color = engine->theme->bg_surface;
    w->fg_color = engine->theme->text_primary;
    w->accent_color = engine->theme->accent;

    /* Assign a stable wire-side group id. 0 is reserved for the
     * scene-canvas root, so start at 1. Each widget keeps the same
     * group_id for its lifetime — the receiver routes incremental
     * DELETE/GROUP updates back to the same entity. */
    if (engine->next_group_id == 0) {
        engine->next_group_id = 1;
    }
    w->group_id = engine->next_group_id++;
    w->dirty = 1;

    return w;
}

void yetty_ygui_widget_init_base(struct yetty_ygui_widget *widget, float x, float y, float w,
                                 float h)
{
    widget->authored_x = x;
    widget->authored_y = y;
    widget->authored_w = w;
    widget->authored_h = h;
    widget->x = x;
    widget->y = y;
    widget->w = w;
    widget->h = h;
}

/* Queue this widget's group_id for emission as a DELETE record on the
 * next render. Skips if the engine hasn't yet sent a first frame (the
 * receiver doesn't know about the widget yet) or if a full redraw is
 * already pending (CMD_ZERO supersedes deletes). */
static void engine_queue_pending_delete(struct yetty_ygui_engine *engine, uint32_t group_id)
{
    if (!engine || engine->needs_full_redraw) {
        return;
    }
    if (engine->pending_delete_count >= engine->pending_delete_cap) {
        uint32_t new_cap = engine->pending_delete_cap ? engine->pending_delete_cap * 2 : 16;
        uint32_t *grown = realloc(engine->pending_deletes, new_cap * sizeof(uint32_t));
        if (!grown) {
            return; /* drop — DELETE will be missing but receiver tolerates */
        }
        engine->pending_deletes = grown;
        engine->pending_delete_cap = new_cap;
    }
    engine->pending_deletes[engine->pending_delete_count++] = group_id;
}

/* Recursively queue DELETE for every widget in this subtree that owns
 * a live entity on the receiver (was_rendered set from the previous
 * frame). Used when a subtree leaves the render pipeline without going
 * through widget_free — i.e. set_visible(false), popup_menu close, …
 * Without this, hidden widgets retain their drawables on the receiver
 * until the next full redraw because the visibility / OPEN gate at
 * render_all early-returns before emit_self_in_group fires.
 *
 * Side effects on each touched widget:
 *   - was_rendered reset to 0 so a later widget_free doesn't re-queue.
 *   - dirty set to 1 so if the subtree comes back visible, every
 *     descendant re-emits a fresh GROUP (even clean ones, whose entity
 *     we just removed on the receiver). */
void yetty_ygui_internal_queue_delete_subtree_rendered(struct yetty_ygui_widget *w)
{
    if (!w || !w->engine) {
        return;
    }
    if (w->was_rendered) {
        engine_queue_pending_delete(w->engine, w->group_id);
        w->was_rendered = 0;
    }
    w->dirty = 1;
    for (struct yetty_ygui_widget *c = w->first_child; c; c = c->next_sibling) {
        yetty_ygui_internal_queue_delete_subtree_rendered(c);
    }
}

void yetty_ygui_widget_free(struct yetty_ygui_widget *widget)
{
    if (!widget) {
        return;
    }

    ydebug("widget_free enter id=%s type=%d ptr=%p", widget->id ? widget->id : "?",
           (int)widget->type, (void *)widget);

    /* Queue a DELETE for this widget's group on the receiver. Children
     * are deleted recursively below; the receiver's DELETE(parent_id)
     * already wipes the whole subtree, so we only queue the top widget
     * being freed. Subtree-internal frees during recursion still call
     * widget_free for each child, but only the topmost freed widget is
     * normally the "user-initiated" delete; we queue all to be safe
     * (DELETE on unknown id is a warn+continue). */
    if (widget->engine && widget->was_rendered) {
        engine_queue_pending_delete(widget->engine, widget->group_id);
    }

    /* Free children recursively */
    struct yetty_ygui_widget *child = widget->first_child;
    while (child) {
        struct yetty_ygui_widget *next = child->next_sibling;
        yetty_ygui_widget_free(child);
        child = next;
    }

    /* Call type-specific destroy via the vtable. */
    if (widget->vtable && widget->vtable->destroy) {
        ydebug("widget_free destroy id=%s", widget->id ? widget->id : "?");
        widget->vtable->destroy(widget);
    }

    ydebug("widget_free finalize id=%s ptr=%p", widget->id ? widget->id : "?", (void *)widget);
    free(widget->id);
    free(widget);
}

/* Sentinel for widget_open_group: caller passes this to widget_close_group
 * when the widget was skipped (no group opened). */
#define YETTY_YGUI_GROUP_SKIPPED UINT32_MAX

/* Open this widget's CMD_GROUP and emit its body (the widget's own
 * drawables, NOT its children). Leaves the group OPEN — the caller is
 * responsible for emitting any nested child groups (typically by
 * recursing into children's render_all) and then calling
 * yetty_ygui_widget_close_group with the returned marker.
 *
 * Returns the group marker on success, or YETTY_YGUI_GROUP_SKIPPED if
 * the widget was skipped (e.g. incremental mode + non-dirty). When
 * skipped, the caller must NOT call close_group.
 *
 * The group rect is parent-RELATIVE: (self->x, self->y, layout_w,
 * layout_h). The receiver accumulates the parent chain to resolve
 * coords back to absolute. */
uint32_t yetty_ygui_widget_open_group(
    struct yetty_ygui_widget *self, struct yetty_ygui_render_ctx *ctx,
    struct yetty_ycore_void_result (*render_fn)(struct yetty_ygui_widget *,
                                                struct yetty_ygui_render_ctx *))
{
    if (!self || !ctx || !ctx->buffer) {
        return YETTY_YGUI_GROUP_SKIPPED;
    }
    /* Skip emission for clean widgets in incremental mode — the receiver
     * already has the entity from a prior frame. */
    if (!self->dirty && !ctx->force_full_redraw) {
        return YETTY_YGUI_GROUP_SKIPPED;
    }
    /* In incremental mode, the receiver's existing entity for this
     * group_id must be wiped before we re-emit a fresh GROUP. CMD_ZERO
     * at the envelope head (force_full_redraw=1) handles this for the
     * whole canvas at once, so no per-widget DELETE is needed there. */
    if (!ctx->force_full_redraw) {
        struct yetty_ycore_void_result dr =
            yetty_ydraw_draw_list_add_cmd_delete(ctx->buffer, self->group_id);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
    }

    /* Group rect is parent-relative (self->x, self->y). Body prim coords
     * are widget-local — ygui paints call render_box(ctx, self->x + dx, …)
     * and the offset trick (ctx->offset_x = -self->x) cancels self->x so
     * the wire carries pure `dx`. */
    float gx = (float)self->x;
    float gy = (float)self->y;
    float gw = (float)self->layout_w;
    float gh = (float)self->layout_h;
    struct yetty_ydraw_id_result mark_res = yetty_ydraw_draw_list_begin_group_with_rect(
        ctx->buffer, self->group_id, gx, gy, gw, gh);
    if (YETTY_IS_ERR(mark_res)) {
        yetty_ycore_error_destroy(mark_res.error);
        return YETTY_YGUI_GROUP_SKIPPED;
    }

    if (render_fn) {
        float saved_off_x = ctx->offset_x;
        float saved_off_y = ctx->offset_y;
        ctx->offset_x = -(float)self->x;
        ctx->offset_y = -(float)self->y;
        struct yetty_ycore_void_result r = render_fn(self, ctx);
        if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
        ctx->offset_x = saved_off_x;
        ctx->offset_y = saved_off_y;
    }

    return mark_res.value;
}

/* Close a group previously opened by yetty_ygui_widget_open_group.
 * NO-OP when `marker == YETTY_YGUI_GROUP_SKIPPED`. */
void yetty_ygui_widget_close_group(
    struct yetty_ygui_widget *self, struct yetty_ygui_render_ctx *ctx, uint32_t marker)
{
    if (!ctx || !ctx->buffer || marker == YETTY_YGUI_GROUP_SKIPPED) {
        return;
    }
    struct yetty_ycore_void_result er = yetty_ydraw_draw_list_end_group(ctx->buffer, marker);
    if (YETTY_IS_ERR(er)) {
        yetty_ycore_error_destroy(er.error);
    }
    if (self) {
        self->dirty = 0;
    }
}

/* Convenience: open group, emit body, close — no nested children. Use
 * for widgets that have no own paint of children inside the group.
 * Custom render_all that recurses into children should call open/close
 * directly so the recursion lands inside the open scope. */
struct yetty_ycore_void_result yetty_ygui_widget_emit_self_in_group(
    struct yetty_ygui_widget *self, struct yetty_ygui_render_ctx *ctx,
    struct yetty_ycore_void_result (*render_fn)(struct yetty_ygui_widget *,
                                                struct yetty_ygui_render_ctx *))
{
    uint32_t marker = yetty_ygui_widget_open_group(self, ctx, render_fn);
    yetty_ygui_widget_close_group(self, ctx, marker);
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_widget_render_all_default(
    struct yetty_ygui_widget *self, struct yetty_ygui_render_ctx *ctx)
{
    /* Skip invisible widgets globally — was previously a per-container concern. */
    if (!(self->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        return YETTY_OK_VOID();
    }

    /* Layout pass already wrote effective_x/y and live x/y/w/h; nothing to
     * recompute here. */
    self->was_rendered = 1;
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();

    /* Open this widget's CMD_GROUP (rect = parent-relative (self->x,
     * self->y, …)), emit body, recurse into children INSIDE the open
     * group scope so each child's CMD_GROUP nests properly, then close.
     * The wire structure mirrors the UI hierarchy 1:1. */
    uint32_t marker = yetty_ygui_widget_open_group(
        self, ctx,
        self->vtable && self->vtable->render ? self->vtable->render : NULL);

    for (struct yetty_ygui_widget *child = self->first_child; child;
         child = child->next_sibling) {
        if (!(child->flags & YETTY_YGUI_FLAG_VISIBLE)) {
            continue;
        }
        struct yetty_ycore_void_result r;
        if (child->vtable && child->vtable->render_all) {
            r = child->vtable->render_all(child, ctx);
        } else {
            r = yetty_ygui_widget_render_all_default(child, ctx);
        }
        if (YETTY_IS_ERR(r)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = r;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
    }

    yetty_ygui_widget_close_group(self, ctx, marker);

    return first_err;
}

/*=============================================================================
 * Widget Hierarchy
 *===========================================================================*/

static void add_to_engine(struct yetty_ygui_engine *engine, struct yetty_ygui_widget *widget)
{
    widget->engine = engine;
    if (!engine->first_widget) {
        engine->first_widget = widget;
        engine->last_widget = widget;
    } else {
        engine->last_widget->next_sibling = widget;
        widget->prev_sibling = engine->last_widget;
        engine->last_widget = widget;
    }
    engine->widget_count++;
    engine->dirty = 1;
}

/* Public-to-other-ygui-TUs wrapper around add_to_engine. The static
 * add_to_engine remains in use for every constructor in this file; the
 * new widget files (ygui_rich.c, ygui_tabbar.c) call this so they don't
 * have to redo the linked-list bookkeeping. */
void yetty_ygui_engine_attach_widget(struct yetty_ygui_engine *engine,
                                     struct yetty_ygui_widget *widget)
{
    if (!engine || !widget) {
        return;
    }
    add_to_engine(engine, widget);
}

/* Recursively re-assign new (higher) group_ids to every widget in the
 * subtree, in DFS order. The new ids come straight off the engine's
 * monotonic counter, so they all rank above every existing widget's
 * id. Mark each widget dirty so the next render re-emits a GROUP
 * record under the new id, and clear was_rendered (the old entity is
 * gone from the receiver after the DELETE flush; this is a fresh
 * entity from the receiver's point of view). */
static void reassign_ids_dfs(struct yetty_ygui_widget *w, struct yetty_ygui_engine *eng)
{
    w->group_id = eng->next_group_id++;
    w->was_rendered = 0;
    w->dirty = 1;
    for (struct yetty_ygui_widget *c = w->first_child; c; c = c->next_sibling) {
        reassign_ids_dfs(c, eng);
    }
}

/* Move a top-level widget to the END of the engine's first_widget chain
 * AND bump its (and its subtree's) group_ids to fresh-from-counter
 * values so the scene-canvas's z-order — which sorts entities by
 * external_id ascending — paints the subtree last (= on top).
 *
 * Why both:
 *   - Chain reorder makes the producer's emit order match the visual
 *     stack: the widget renders last in the engine's main loop, so any
 *     DELETE / GROUP records for its subtree show up at the end of the
 *     envelope (slight latency win for the receiver too).
 *   - Group_id bump is the real z-order knob. The receiver's
 *     scene-grid buckets prims by entity external_id ASCENDING and the
 *     shader paints buckets in that order. Just shuffling chain
 *     positions doesn't change external_ids, so without this bump the
 *     popup would keep painting behind widgets that were created later
 *     and therefore have higher ids.
 *
 * No full redraw needed — incremental DELETE(old_id) for every widget
 * in the subtree (queued via queue_delete_subtree_rendered) plus a
 * fresh GROUP(new_id) on the next emission is the only delta. Other
 * widgets are untouched.
 *
 * No-op for widgets that have been re-parented (those are not in the
 * engine's top-level chain and don't own a z-order independent of
 * their parent). */
void yetty_ygui_internal_bring_to_front(struct yetty_ygui_widget *widget)
{
    if (!widget || !widget->engine || widget->parent) {
        return;
    }
    struct yetty_ygui_engine *eng = widget->engine;

    /* Step 1: queue DELETE records for every entity in the subtree so
     * the receiver wipes their old buckets before we re-emit under new
     * ids. After this call every widget in the subtree has dirty=1 and
     * was_rendered=0 — the next render will emit fresh GROUP records. */
    yetty_ygui_internal_queue_delete_subtree_rendered(widget);

    /* Step 2: assign fresh group_ids in DFS order so the subtree's
     * internal z-order (parent below children) is preserved at the new
     * id range. */
    reassign_ids_dfs(widget, eng);

    /* Step 3: move to the end of the engine chain so render emits this
     * subtree last (matches the new id ordering and keeps the wire
     * trace easy to follow). */
    if (eng->last_widget != widget) {
        if (widget->prev_sibling) {
            widget->prev_sibling->next_sibling = widget->next_sibling;
        } else if (eng->first_widget == widget) {
            eng->first_widget = widget->next_sibling;
        }
        if (widget->next_sibling) {
            widget->next_sibling->prev_sibling = widget->prev_sibling;
        }
        widget->prev_sibling = eng->last_widget;
        widget->next_sibling = NULL;
        if (eng->last_widget) {
            eng->last_widget->next_sibling = widget;
        } else {
            eng->first_widget = widget;
        }
        eng->last_widget = widget;
    }

    eng->dirty = 1;
}

void yetty_ygui_widget_add_child(struct yetty_ygui_widget *parent, struct yetty_ygui_widget *child)
{
    if (!parent || !child) {
        return;
    }

    /* Remove from engine's top-level list if present */
    struct yetty_ygui_engine *engine = parent->engine;
    if (engine && !child->parent) {
        if (child->prev_sibling) {
            child->prev_sibling->next_sibling = child->next_sibling;
        } else if (engine->first_widget == child) {
            engine->first_widget = child->next_sibling;
        }
        if (child->next_sibling) {
            child->next_sibling->prev_sibling = child->prev_sibling;
        } else if (engine->last_widget == child) {
            engine->last_widget = child->prev_sibling;
        }
        engine->widget_count--;
    }

    child->parent = parent;
    child->prev_sibling = NULL;
    child->next_sibling = NULL;

    if (!parent->first_child) {
        parent->first_child = child;
        parent->last_child = child;
    } else {
        parent->last_child->next_sibling = child;
        child->prev_sibling = parent->last_child;
        parent->last_child = child;
    }

    if (engine) {
        engine->dirty = 1;
    }
}

void yetty_ygui_widget_remove_child(struct yetty_ygui_widget *parent,
                                    struct yetty_ygui_widget *child)
{
    if (!parent || !child || child->parent != parent) {
        return;
    }

    if (child->prev_sibling) {
        child->prev_sibling->next_sibling = child->next_sibling;
    } else {
        parent->first_child = child->next_sibling;
    }

    if (child->next_sibling) {
        child->next_sibling->prev_sibling = child->prev_sibling;
    } else {
        parent->last_child = child->prev_sibling;
    }

    child->parent = NULL;
    child->prev_sibling = NULL;
    child->next_sibling = NULL;

    if (parent->engine) {
        parent->engine->dirty = 1; parent->dirty = 1;
        /* Unparented subtree leaves the render pipeline. Queue DELETE
         * for the child AND every rendered descendant — the receiver's
         * scene-canvas keeps each widget as an independent root-level
         * entity, so unparenting only the top widget would orphan its
         * descendants. The helper resets was_rendered + sets dirty=1
         * across the subtree so a re-attach later emits fresh GROUPs. */
        yetty_ygui_internal_queue_delete_subtree_rendered(child);
    }
}

void yetty_ygui_widget_remove(struct yetty_ygui_widget *widget)
{
    if (!widget) {
        return;
    }

    if (widget->parent) {
        yetty_ygui_widget_remove_child(widget->parent, widget);
    } else if (widget->engine) {
        struct yetty_ygui_engine *engine = widget->engine;
        if (widget->prev_sibling) {
            widget->prev_sibling->next_sibling = widget->next_sibling;
        } else {
            engine->first_widget = widget->next_sibling;
        }
        if (widget->next_sibling) {
            widget->next_sibling->prev_sibling = widget->prev_sibling;
        } else {
            engine->last_widget = widget->prev_sibling;
        }
        engine->widget_count--;
        engine->dirty = 1;
    }

    yetty_ygui_widget_free(widget);
}

struct yetty_ygui_widget *yetty_ygui_widget_parent(struct yetty_ygui_widget *widget)
{
    return widget ? widget->parent : NULL;
}

struct yetty_ygui_widget *yetty_ygui_widget_first_child(struct yetty_ygui_widget *widget)
{
    return widget ? widget->first_child : NULL;
}

struct yetty_ygui_widget *yetty_ygui_widget_next_sibling(struct yetty_ygui_widget *widget)
{
    return widget ? widget->next_sibling : NULL;
}

/*=============================================================================
 * Widget Properties (Generic)
 *===========================================================================*/

const char *yetty_ygui_widget_id(const struct yetty_ygui_widget *widget)
{
    return widget ? widget->id : NULL;
}

ygui_widget_type_t yetty_ygui_widget_type(const struct yetty_ygui_widget *widget)
{
    return widget ? widget->type : YETTY_YGUI_WIDGET_CUSTOM;
}

void yetty_ygui_widget_set_position(struct yetty_ygui_widget *widget, float x, float y)
{
    if (!widget) {
        return;
    }
    widget->authored_x = x;
    widget->authored_y = y;
    widget->x = x;
    widget->y = y;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

struct pixel_coord_result yetty_ygui_widget_get_position(
    const struct yetty_ygui_widget *widget)
{
    if (!widget) {
        return YETTY_ERR(pixel_coord, "widget_get_position: NULL widget");
    }
    struct yetty_ycore_pixel_coord pos = {widget->x, widget->y};
    return YETTY_OK(pixel_coord, pos);
}

void yetty_ygui_widget_set_size(struct yetty_ygui_widget *widget, float w, float h)
{
    if (!widget) {
        return;
    }
    widget->authored_w = w;
    widget->authored_h = h;
    widget->w = w;
    widget->h = h;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

struct pixel_size_result yetty_ygui_widget_get_size(
    const struct yetty_ygui_widget *widget)
{
    if (!widget) {
        return YETTY_ERR(pixel_size, "widget_get_size: NULL widget");
    }
    /* Report authored size — the user-visible value, stable across resizes. */
    struct yetty_ycore_pixel_size size = {widget->authored_w, widget->authored_h};
    return YETTY_OK(pixel_size, size);
}

struct rectangle_result yetty_ygui_widget_get_layout_box(
    const struct yetty_ygui_widget *widget)
{
    if (!widget) {
        return YETTY_ERR(rectangle, "widget_get_layout_box: NULL widget");
    }
    struct yetty_ycore_rectangle box = {
        {widget->layout_x, widget->layout_y},
        {widget->layout_x + widget->layout_w, widget->layout_y + widget->layout_h},
    };
    return YETTY_OK(rectangle, box);
}

struct rectangle_result yetty_ygui_widget_get_content_box(
    const struct yetty_ygui_widget *widget)
{
    if (!widget) {
        return YETTY_ERR(rectangle, "widget_get_content_box: NULL widget");
    }
    struct yetty_ycore_rectangle box = {
        {widget->content_x, widget->content_y},
        {widget->content_x + widget->content_w, widget->content_y + widget->content_h},
    };
    return YETTY_OK(rectangle, box);
}

void yetty_ygui_widget_set_visible(struct yetty_ygui_widget *widget, int visible)
{
    if (!widget) {
        return;
    }
    int was_visible = (widget->flags & YETTY_YGUI_FLAG_VISIBLE) != 0;
    if (visible) {
        widget->flags |= YETTY_YGUI_FLAG_VISIBLE;
    } else {
        widget->flags &= ~YETTY_YGUI_FLAG_VISIBLE;
    }
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
        /* visible → hidden: render_all_default will early-return for
         * this subtree, so its entities on the receiver will never get
         * a DELETE through emit_self_in_group. Queue them explicitly. */
        if (was_visible && !visible) {
            yetty_ygui_internal_queue_delete_subtree_rendered(widget);
        }
    }
}

int yetty_ygui_widget_is_visible(const struct yetty_ygui_widget *widget)
{
    return widget ? (widget->flags & YETTY_YGUI_FLAG_VISIBLE) != 0 : 0;
}

void yetty_ygui_widget_set_enabled(struct yetty_ygui_widget *widget, int enabled)
{
    if (!widget) {
        return;
    }
    if (enabled) {
        widget->flags &= ~YETTY_YGUI_FLAG_DISABLED;
    } else {
        widget->flags |= YETTY_YGUI_FLAG_DISABLED;
    }
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

int yetty_ygui_widget_is_enabled(const struct yetty_ygui_widget *widget)
{
    return widget ? (widget->flags & YETTY_YGUI_FLAG_DISABLED) == 0 : 0;
}

uint32_t yetty_ygui_widget_get_flags(const struct yetty_ygui_widget *widget)
{
    return widget ? widget->flags : 0;
}

void yetty_ygui_widget_set_bg_color(struct yetty_ygui_widget *widget, uint32_t color)
{
    if (!widget) {
        return;
    }
    widget->bg_color = color;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

void yetty_ygui_widget_set_fg_color(struct yetty_ygui_widget *widget, uint32_t color)
{
    if (!widget) {
        return;
    }
    widget->fg_color = color;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

void yetty_ygui_widget_set_accent_color(struct yetty_ygui_widget *widget, uint32_t color)
{
    if (!widget) {
        return;
    }
    widget->accent_color = color;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

/*=============================================================================
 * Layout setters (flexbox)
 *===========================================================================*/

static void layout_widget_dirty(struct yetty_ygui_widget *widget)
{
    if (widget && widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

void yetty_ygui_widget_set_layout_mode(struct yetty_ygui_widget *widget, ygui_layout_mode_t mode)
{
    if (!widget) {
        return;
    }
    widget->layout.mode = mode;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_flex_direction(struct yetty_ygui_widget *widget,
                                          ygui_flex_direction_t direction)
{
    if (!widget) {
        return;
    }
    widget->layout.direction = direction;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_justify_content(struct yetty_ygui_widget *widget, ygui_justify_t justify)
{
    if (!widget) {
        return;
    }
    widget->layout.justify_content = justify;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_align_items(struct yetty_ygui_widget *widget, ygui_align_t align)
{
    if (!widget) {
        return;
    }
    widget->layout.align_items = align;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_align_self(struct yetty_ygui_widget *widget, ygui_align_t align)
{
    if (!widget) {
        return;
    }
    widget->layout.align_self = align;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_flex(struct yetty_ygui_widget *widget, float grow, float shrink,
                                float basis)
{
    if (!widget) {
        return;
    }
    widget->layout.flex_grow = grow;
    widget->layout.flex_shrink = shrink;
    widget->layout.flex_basis = basis;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_gap(struct yetty_ygui_widget *widget, float gap)
{
    if (!widget) {
        return;
    }
    widget->layout.gap = gap;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_padding(struct yetty_ygui_widget *widget, float top, float right,
                                   float bottom, float left)
{
    if (!widget) {
        return;
    }
    widget->layout.padding_top = top;
    widget->layout.padding_right = right;
    widget->layout.padding_bottom = bottom;
    widget->layout.padding_left = left;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_margin(struct yetty_ygui_widget *widget, float top, float right,
                                  float bottom, float left)
{
    if (!widget) {
        return;
    }
    widget->layout.margin_top = top;
    widget->layout.margin_right = right;
    widget->layout.margin_bottom = bottom;
    widget->layout.margin_left = left;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_min_size(struct yetty_ygui_widget *widget, float min_w, float min_h)
{
    if (!widget) {
        return;
    }
    widget->layout.min_w = min_w;
    widget->layout.min_h = min_h;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_max_size(struct yetty_ygui_widget *widget, float max_w, float max_h)
{
    if (!widget) {
        return;
    }
    widget->layout.max_w = max_w;
    widget->layout.max_h = max_h;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_flex_wrap(struct yetty_ygui_widget *widget, ygui_flex_wrap_t wrap)
{
    if (!widget) {
        return;
    }
    widget->layout.wrap = wrap;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_align_content(struct yetty_ygui_widget *widget, ygui_align_t align)
{
    if (!widget) {
        return;
    }
    widget->layout.align_content = align;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_position_mode(struct yetty_ygui_widget *widget, ygui_position_t pos)
{
    if (!widget) {
        return;
    }
    widget->layout.position = pos;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_flex_basis_percent(struct yetty_ygui_widget *widget, float pct)
{
    if (!widget) {
        return;
    }
    widget->layout.flex_basis_percent = pct;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_size_percent(struct yetty_ygui_widget *widget, float w_pct, float h_pct)
{
    if (!widget) {
        return;
    }
    widget->layout.width_percent = w_pct;
    widget->layout.height_percent = h_pct;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_min_size_percent(struct yetty_ygui_widget *widget, float min_w_pct,
                                            float min_h_pct)
{
    if (!widget) {
        return;
    }
    widget->layout.min_w_percent = min_w_pct;
    widget->layout.min_h_percent = min_h_pct;
    layout_widget_dirty(widget);
}

void yetty_ygui_widget_set_max_size_percent(struct yetty_ygui_widget *widget, float max_w_pct,
                                            float max_h_pct)
{
    if (!widget) {
        return;
    }
    widget->layout.max_w_percent = max_w_pct;
    widget->layout.max_h_percent = max_h_pct;
    layout_widget_dirty(widget);
}

/*=============================================================================
 * Button Widget
 *===========================================================================*/

static struct yetty_ycore_void_result button_render(struct yetty_ygui_widget *self,
                                                    struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    int pressed = (self->flags & YETTY_YGUI_FLAG_PRESSED) != 0;
    int hovered = (self->flags & YETTY_YGUI_FLAG_HOVER) != 0;
    int focused = (self->flags & YETTY_YGUI_FLAG_FOCUSED) != 0;

    /* Surface base color: hover brightens, press goes to accent. */
    uint32_t surface = pressed ? self->accent_color : (hovered ? t->bg_hover : self->bg_color);

    /* Material-style elevation: low when idle, drops to ~0 when pressed
     * so the button looks "depressed" against the page. */
    float elev = pressed ? 0.0f : t->elevation_low;
    /* Press also nudges the surface down 1px to give tactile feedback. */
    float press_offset = pressed ? 1.0f : 0.0f;

    /* Drop shadow first (skipped in pressed state). */
    yetty_ygui_render_ctx_render_box_shadow(ctx, self->x, self->y, self->w, self->h,
                                            t->radius_medium, elev, t->shadow, t->elevation_alpha);

    /* Surface — flat color, or a real linear gradient when the theme
     * opts in. Using the SDF gradient primitive (ported from yetty-poc):
     * top edge is `surface` lightened, bottom edge is `surface` darkened,
     * giving a subtle convex feel without painting an obvious overlay. */
    if (t->enable_gradient && !pressed) {
        uint32_t top = surface;
        uint32_t bot = surface;
        /* Bias top by +10% white, bottom by -10% black, alpha-preserving. */
        uint8_t r = (uint8_t)(surface & 0xFF);
        uint8_t g = (uint8_t)((surface >> 8) & 0xFF);
        uint8_t b = (uint8_t)((surface >> 16) & 0xFF);
        uint8_t a = (uint8_t)((surface >> 24) & 0xFF);
        uint8_t lr = (uint8_t)((r * 230 + 255 * 25) / 255);
        uint8_t lg = (uint8_t)((g * 230 + 255 * 25) / 255);
        uint8_t lb = (uint8_t)((b * 230 + 255 * 25) / 255);
        uint8_t dr = (uint8_t)(r * 230 / 255);
        uint8_t dg = (uint8_t)(g * 230 / 255);
        uint8_t db = (uint8_t)(b * 230 / 255);
        top = (uint32_t)a << 24 | (uint32_t)lb << 16 | (uint32_t)lg << 8 | lr;
        bot = (uint32_t)a << 24 | (uint32_t)db << 16 | (uint32_t)dg << 8 | dr;
        yetty_ygui_render_ctx_render_box_linear_gradient(
            ctx, self->x, self->y + press_offset, self->w, self->h, t->radius_medium,
            /*gx0,gy0=*/self->x, self->y + press_offset,
            /*gx1,gy1=*/self->x, self->y + press_offset + self->h, top, bot);
    } else {
        yetty_ygui_render_ctx_render_box(ctx, self->x, self->y + press_offset, self->w, self->h,
                                         surface, t->radius_medium);
    }

    /* Frame around the button: only shows while the cursor is over it.
     * Focus state alone is not enough — focus persists after click and
     * the user expects the highlight to leave with the cursor. The
     * focus-ring variant (slightly bigger, thicker) replaces the hover
     * halo when the button has been focused (e.g. after a click); the
     * hover halo is the lighter "just hovering" state. Neither renders
     * when the cursor is elsewhere. */
    if (hovered && !pressed) {
        if (focused) {
            float r = t->radius_medium + 2.0f;
            yetty_ygui_render_ctx_render_box_outline(
                ctx, self->x - 2.0f, self->y - 2.0f + press_offset, self->w + 4.0f,
                self->h + 4.0f, self->accent_color, r, 2.0f);
        } else {
            yetty_ygui_render_ctx_render_box_outline(
                ctx, self->x, self->y + press_offset, self->w, self->h, self->accent_color,
                t->radius_medium, 1.5f);
        }
    }

    /* Label. */
    if (self->data.button.label) {
        yetty_ygui_render_ctx_render_text(ctx, self->data.button.label, self->x + t->pad_large,
                                          self->y + t->pad_medium + press_offset, self->fg_color,
                                          t->font_size);
    }
    return YETTY_OK_VOID();
}

static int button_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)lx;
    (void)ly;
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_PRESS;
    return 1;
}

static int button_on_release(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    if (lx >= 0 && lx < self->w && ly >= 0 && ly < self->h) {
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_CLICK;
        return 1;
    }
    return 0;
}

static void button_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.button.label);
}

static float button_baseline(const struct yetty_ygui_widget *self,
                             const struct yetty_ygui_theme *theme)
{
    /* Mirror button_render: text drawn at y = self->y + pad_medium and the
     * helper places it at baseline = top + font_size * 0.8. */
    (void)self;
    return theme->pad_medium + theme->font_size * 0.8f;
}

struct yetty_ygui_widget *yetty_ygui_engine_button(struct yetty_ygui_engine *engine, const char *id,
                                                   float x, float y, float w, float h,
                                                   const char *label)
{
    struct yetty_ygui_widget *btn =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_BUTTON, id);
    if (!btn) {
        return NULL;
    }

    yetty_ygui_widget_init_base(btn, x, y, w, h);
    btn->data.button.label = ygui_strdup(label);
    static const struct yetty_ygui_widget_vtable button_vtable = {
        .render = button_render,
        .on_press = button_on_press,
        .on_release = button_on_release,
        .destroy = button_destroy,
        .baseline_offset = button_baseline,
    };
    btn->vtable = &button_vtable;

    add_to_engine(engine, btn);
    return btn;
}

void yetty_ygui_widget_button_set_label(struct yetty_ygui_widget *widget, const char *label)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_BUTTON) {
        return;
    }
    free(widget->data.button.label);
    widget->data.button.label = ygui_strdup(label);
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

const char *yetty_ygui_widget_button_get_label(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_BUTTON) {
        return NULL;
    }
    return widget->data.button.label;
}

/*=============================================================================
 * Label Widget
 *===========================================================================*/

static struct yetty_ycore_void_result label_render(struct yetty_ygui_widget *self,
                                                   struct yetty_ygui_render_ctx *ctx)
{
    if (self->data.label.text) {
        float font_size =
            self->data.label.font_size > 0 ? self->data.label.font_size : ctx->theme->font_size;
        yetty_ygui_render_ctx_render_text(ctx, self->data.label.text, self->x, self->y,
                                          self->fg_color, font_size);
    }
    return YETTY_OK_VOID();
}

static void label_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.label.text);
}

static float label_baseline(const struct yetty_ygui_widget *self,
                            const struct yetty_ygui_theme *theme)
{
    /* Label draws at (self->x, self->y); render_text places baseline at
     * top + font_size * 0.8. */
    float fs = self->data.label.font_size > 0 ? self->data.label.font_size : theme->font_size;
    return fs * 0.8f;
}

struct yetty_ygui_widget *yetty_ygui_engine_label(struct yetty_ygui_engine *engine, const char *id,
                                                  float x, float y, const char *text)
{
    struct yetty_ygui_widget *lbl =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_LABEL, id);
    if (!lbl) {
        return NULL;
    }

    float h = engine->theme->row_height;
    yetty_ygui_widget_init_base(lbl, x, y, 100, h); /* Width is flexible */
    lbl->data.label.text = ygui_strdup(text);
    lbl->data.label.font_size = 0; /* Use theme default */
    static const struct yetty_ygui_widget_vtable label_vtable = {
        .render = label_render,
        .destroy = label_destroy,
        .baseline_offset = label_baseline,
    };
    lbl->vtable = &label_vtable;

    add_to_engine(engine, lbl);
    return lbl;
}

void yetty_ygui_widget_label_set_text(struct yetty_ygui_widget *widget, const char *text)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_LABEL) {
        return;
    }
    free(widget->data.label.text);
    widget->data.label.text = ygui_strdup(text);
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

const char *yetty_ygui_widget_label_get_text(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_LABEL) {
        return NULL;
    }
    return widget->data.label.text;
}

void yetty_ygui_widget_label_set_font_size(struct yetty_ygui_widget *widget, float size)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_LABEL) {
        return;
    }
    widget->data.label.font_size = size;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

/*=============================================================================
 * Slider Widget
 *===========================================================================*/

static struct yetty_ycore_void_result slider_render(struct yetty_ygui_widget *self,
                                                    struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float track_h = t->pad_medium;
    float track_y = self->y + (self->h - track_h) / 2;

    /* Track background */
    yetty_ygui_render_ctx_render_box(ctx, self->x, track_y, self->w, track_h, self->bg_color,
                                     t->radius_small);

    /* Filled portion */
    float range = self->data.slider.max_val - self->data.slider.min_val;
    float pct = range > 0 ? (self->data.slider.value - self->data.slider.min_val) / range : 0;
    float fill_w = pct * self->w;
    yetty_ygui_render_ctx_render_box(ctx, self->x, track_y, fill_w, track_h, self->accent_color,
                                     t->radius_small);

    /* Handle */
    float handle_w = t->scrollbar_size;
    float handle_x = self->x + fill_w - handle_w / 2;
    yetty_ygui_render_ctx_render_box(ctx, handle_x, self->y, handle_w, self->h, self->accent_color,
                                     handle_w / 2);
    return YETTY_OK_VOID();
}

static void slider_update_value(struct yetty_ygui_widget *self, float local_x)
{
    float pct = ygui_clamp(local_x / self->w, 0.0f, 1.0f);
    float range = self->data.slider.max_val - self->data.slider.min_val;
    self->data.slider.value = self->data.slider.min_val + pct * range;
}

static int slider_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)ly;
    slider_update_value(self, lx);

    /* Call user callback */
    if (self->change_callback) {
        self->change_callback(self, self->data.slider.value, self->change_userdata);
    }

    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.float_value = self->data.slider.value;
    return 1;
}

static int slider_on_drag(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)ly;
    slider_update_value(self, lx);

    /* Call user callback */
    if (self->change_callback) {
        self->change_callback(self, self->data.slider.value, self->change_userdata);
    }

    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.float_value = self->data.slider.value;
    return 1;
}

static int slider_on_scroll(struct yetty_ygui_widget *self, float dx, float dy, ygui_event_t *out)
{
    (void)dx;
    float range = self->data.slider.max_val - self->data.slider.min_val;
    float delta = dy * range * 0.05f;
    self->data.slider.value = ygui_clamp(self->data.slider.value + delta, self->data.slider.min_val,
                                         self->data.slider.max_val);
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.float_value = self->data.slider.value;
    return 1;
}

struct yetty_ygui_widget *yetty_ygui_engine_slider(struct yetty_ygui_engine *engine, const char *id,
                                                   float x, float y, float w, float h,
                                                   float min_val, float max_val, float value)
{
    struct yetty_ygui_widget *sld =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_SLIDER, id);
    if (!sld) {
        return NULL;
    }

    yetty_ygui_widget_init_base(sld, x, y, w, h);
    sld->data.slider.min_val = min_val;
    sld->data.slider.max_val = max_val;
    sld->data.slider.value = ygui_clamp(value, min_val, max_val);
    static const struct yetty_ygui_widget_vtable slider_vtable = {
        .render = slider_render,
        .on_press = slider_on_press,
        .on_drag = slider_on_drag,
        .on_scroll = slider_on_scroll,
    };
    sld->vtable = &slider_vtable;

    add_to_engine(engine, sld);
    return sld;
}

void yetty_ygui_widget_slider_set_value(struct yetty_ygui_widget *widget, float value)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SLIDER) {
        return;
    }
    widget->data.slider.value =
        ygui_clamp(value, widget->data.slider.min_val, widget->data.slider.max_val);
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

float yetty_ygui_widget_slider_get_value(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SLIDER) {
        return 0;
    }
    return widget->data.slider.value;
}

void yetty_ygui_widget_slider_set_range(struct yetty_ygui_widget *widget, float min_val,
                                        float max_val)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SLIDER) {
        return;
    }
    widget->data.slider.min_val = min_val;
    widget->data.slider.max_val = max_val;
    widget->data.slider.value = ygui_clamp(widget->data.slider.value, min_val, max_val);
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

/*=============================================================================
 * Checkbox Widget
 *===========================================================================*/

static struct yetty_ycore_void_result checkbox_render(struct yetty_ygui_widget *self,
                                                      struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float box_size = self->h - t->pad_small * 2;
    float box_y = self->y + t->pad_small;

    /* Box background */
    uint32_t box_color = self->data.checkbox.checked ? self->accent_color : self->bg_color;
    yetty_ygui_render_ctx_render_box(ctx, self->x, box_y, box_size, box_size, box_color,
                                     t->radius_small);
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, box_y, box_size, box_size, t->border,
                                             t->radius_small, 1.5f);

    /* Checkmark (simple cross for now) */
    if (self->data.checkbox.checked) {
        float cx = self->x + box_size / 2;
        float cy = box_y + box_size / 2;
        float s = box_size * 0.3f;
        /* Draw a simple checkmark using triangles */
        yetty_ygui_render_ctx_render_box(ctx, cx - s, cy - 1, s * 2, 3, self->fg_color, 1);
    }

    /* Label */
    if (self->data.checkbox.label) {
        float text_x = self->x + box_size + t->pad_medium;
        yetty_ygui_render_ctx_render_text(ctx, self->data.checkbox.label, text_x,
                                          self->y + t->pad_medium, self->fg_color, t->font_size);
    }
    return YETTY_OK_VOID();
}

static int checkbox_on_release(struct yetty_ygui_widget *self, float lx, float ly,
                               ygui_event_t *out)
{
    if (lx >= 0 && lx < self->w && ly >= 0 && ly < self->h) {
        self->data.checkbox.checked = !self->data.checkbox.checked;

        /* Call user callback */
        if (self->check_callback) {
            self->check_callback(self, self->data.checkbox.checked, self->check_userdata);
        }

        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_CHANGE;
        out->data.bool_value = self->data.checkbox.checked;
        return 1;
    }
    return 0;
}

static void checkbox_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.checkbox.label);
}

struct yetty_ygui_widget *yetty_ygui_engine_checkbox(struct yetty_ygui_engine *engine,
                                                     const char *id, float x, float y, float w,
                                                     float h, const char *label, int checked)
{
    struct yetty_ygui_widget *chk =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_CHECKBOX, id);
    if (!chk) {
        return NULL;
    }

    yetty_ygui_widget_init_base(chk, x, y, w, h);
    chk->data.checkbox.label = ygui_strdup(label);
    chk->data.checkbox.checked = checked;
    static const struct yetty_ygui_widget_vtable checkbox_vtable = {
        .render = checkbox_render,
        .on_release = checkbox_on_release,
        .destroy = checkbox_destroy,
    };
    chk->vtable = &checkbox_vtable;

    add_to_engine(engine, chk);
    return chk;
}

void yetty_ygui_widget_checkbox_set_checked(struct yetty_ygui_widget *widget, int checked)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_CHECKBOX) {
        return;
    }
    widget->data.checkbox.checked = checked;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

int yetty_ygui_widget_checkbox_get_checked(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_CHECKBOX) {
        return 0;
    }
    return widget->data.checkbox.checked;
}

void yetty_ygui_widget_checkbox_set_label(struct yetty_ygui_widget *widget, const char *label)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_CHECKBOX) {
        return;
    }
    free(widget->data.checkbox.label);
    widget->data.checkbox.label = ygui_strdup(label);
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

/*=============================================================================
 * Panel Widget
 *===========================================================================*/

static struct yetty_ycore_void_result panel_render(struct yetty_ygui_widget *self,
                                                   struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float radius =
        self->data.panel.corner_radius > 0 ? self->data.panel.corner_radius : t->radius_large;

    /* Soft elevation underneath. Panels use medium elevation by default. */
    yetty_ygui_render_ctx_render_box_shadow(ctx, self->x, self->y, self->w, self->h, radius,
                                            t->elevation_medium, t->shadow, t->elevation_alpha);

    /* Background */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, self->bg_color,
                                     radius);

    /* Scrollbar if needed */
    float scrollable_h = self->h - self->data.panel.header_h;
    float content_h = self->data.panel.content_h;
    if (content_h > scrollable_h && scrollable_h > 0) {
        float sb_w = t->scrollbar_size;
        float track_x = self->x + self->w - sb_w;
        float track_y = self->y + self->data.panel.header_h;
        float track_h = scrollable_h;

        /* Track */
        yetty_ygui_render_ctx_render_box(ctx, track_x, track_y, sb_w, track_h, t->bg_secondary,
                                         sb_w / 2);

        /* Thumb */
        float max_scroll = content_h - scrollable_h;
        float thumb_h = ygui_max(20.0f, track_h * scrollable_h / content_h);
        float thumb_range = track_h - thumb_h;
        float thumb_y =
            track_y + (max_scroll > 0 ? (self->data.panel.scroll_y / max_scroll) * thumb_range : 0);
        yetty_ygui_render_ctx_render_box(ctx, track_x + t->pad_small, thumb_y, sb_w - t->pad_medium,
                                         thumb_h, t->thumb_normal, (sb_w - t->pad_medium) / 2);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result panel_render_all(struct yetty_ygui_widget *self,
                                                       struct yetty_ygui_render_ctx *ctx)
{
    self->effective_x = self->x + ctx->offset_x;
    self->effective_y = self->y + ctx->offset_y;
    self->was_rendered = 1;

    const struct yetty_ygui_theme *t = ctx->theme;
    float header_h = self->data.panel.header_h;
    float scrollable_h = self->h - header_h;
    float content_h = self->data.panel.content_h;
    float sb_w = (content_h > scrollable_h && scrollable_h > 0) ? t->scrollbar_size : 0;
    (void)sb_w;

    /* Open the panel's CMD_GROUP (parent-relative rect), emit body
     * (background + scrollbar), then recurse children INSIDE the open
     * group so each child's CMD_GROUP nests under this one. Children
     * coords are already widget-local; scroll offset is applied as a
     * negative offset on the ctx so render_box positions reflect the
     * scroll state. Note: with the nested wire model, the receiver
     * accumulates the parent rect for absolute positions, so scrolling
     * via ctx offset stays a widget-local concern.
     *
     * For TWO populations (header vs scrollable), we use the existing
     * ctx->offset shift only for the scrollable subset — header
     * children stay at their layout coords. */
    uint32_t marker = yetty_ygui_widget_open_group(self, ctx, panel_render);

    /* Header children (no scroll). offset stays whatever the caller
     * had set for body emission since render_box adds it. We don't
     * need to shift here because child render_all uses the child's
     * own (parent-relative) x/y for its CMD_GROUP rect. */
    for (struct yetty_ygui_widget *child = self->first_child; child;
         child = child->next_sibling) {
        if (child->y < header_h) {
            if (child->vtable && child->vtable->render_all) {
                child->vtable->render_all(child, ctx);
            } else {
                yetty_ygui_widget_render_all_default(child, ctx);
            }
        }
    }

    /* Scrollable children — the panel's scroll state is a per-child
     * y-offset that lives in the CMD_GROUP rect we emit for each child
     * (in the future this should become a property of THIS group's
     * rect or a dedicated scroll uniform — for now we adjust each
     * child's effective position via temporary mutation of the
     * child's stored y). */
    float scroll_x = self->data.panel.scroll_x;
    float scroll_y = self->data.panel.scroll_y;
    for (struct yetty_ygui_widget *child = self->first_child; child;
         child = child->next_sibling) {
        if (child->y >= header_h) {
            /* TODO: proper clipping */
            float saved_x = (float)child->x;
            float saved_y = (float)child->y;
            child->x = (int)(saved_x - scroll_x);
            child->y = (int)(saved_y - scroll_y);
            if (child->vtable && child->vtable->render_all) {
                child->vtable->render_all(child, ctx);
            } else {
                yetty_ygui_widget_render_all_default(child, ctx);
            }
            child->x = (int)saved_x;
            child->y = (int)saved_y;
        }
    }

    yetty_ygui_widget_close_group(self, ctx, marker);
    return YETTY_OK_VOID();
}

static int panel_on_scroll(struct yetty_ygui_widget *self, float dx, float dy, ygui_event_t *out)
{
    (void)dx;
    float scrollable_h = self->h - self->data.panel.header_h;
    float max_scroll = ygui_max(0, self->data.panel.content_h - scrollable_h);
    float speed = 20.0f; /* TODO: get from theme */

    self->data.panel.scroll_y = ygui_clamp(self->data.panel.scroll_y - dy * speed, 0, max_scroll);

    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_SCROLL;
    out->data.scroll.x = self->data.panel.scroll_x;
    out->data.scroll.y = self->data.panel.scroll_y;
    return 1;
}

struct yetty_ygui_widget *yetty_ygui_engine_panel(struct yetty_ygui_engine *engine, const char *id,
                                                  float x, float y, float w, float h)
{
    struct yetty_ygui_widget *pnl =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_PANEL, id);
    if (!pnl) {
        return NULL;
    }

    yetty_ygui_widget_init_base(pnl, x, y, w, h);
    pnl->data.panel.scroll_x = 0;
    pnl->data.panel.scroll_y = 0;
    pnl->data.panel.content_w = w;
    pnl->data.panel.content_h = h;
    pnl->data.panel.header_h = 0;
    pnl->data.panel.corner_radius = 0;
    static const struct yetty_ygui_widget_vtable panel_vtable = {
        .render = panel_render,
        .render_all = panel_render_all,
        .on_scroll = panel_on_scroll,
    };
    pnl->vtable = &panel_vtable;

    add_to_engine(engine, pnl);
    return pnl;
}

void yetty_ygui_widget_panel_set_scroll(struct yetty_ygui_widget *widget, float x, float y)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_PANEL) {
        return;
    }
    widget->data.panel.scroll_x = x;
    widget->data.panel.scroll_y = y;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

void yetty_ygui_widget_panel_get_scroll(const struct yetty_ygui_widget *widget, float *x, float *y)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_PANEL) {
        return;
    }
    if (x) {
        *x = widget->data.panel.scroll_x;
    }
    if (y) {
        *y = widget->data.panel.scroll_y;
    }
}

void yetty_ygui_widget_panel_set_content_size(struct yetty_ygui_widget *widget, float w, float h)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_PANEL) {
        return;
    }
    widget->data.panel.content_w = w;
    widget->data.panel.content_h = h;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

void yetty_ygui_widget_panel_set_header_height(struct yetty_ygui_widget *widget, float h)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_PANEL) {
        return;
    }
    widget->data.panel.header_h = h;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

/*=============================================================================
 * Progress Widget
 *===========================================================================*/

static struct yetty_ycore_void_result progress_render(struct yetty_ygui_widget *self,
                                                      struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;

    /* Background track */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, self->bg_color,
                                     t->radius_small);

    if (self->data.progress.indeterminate) {
        /* Sliding slug across the track. Phase advances per render
         * frame; the slug is 25% of the track width and oscillates
         * smoothly from left edge to right edge. Engine marks the
         * widget dirty every frame the bar is visible so renders
         * keep firing. */
        self->data.progress.anim_phase += 0.02f;
        if (self->data.progress.anim_phase > 1.0f) {
            self->data.progress.anim_phase -= 1.0f;
        }
        float slug_w = self->w * 0.25f;
        float travel = self->w - slug_w;
        float p = self->data.progress.anim_phase;
        /* Triangle wave (0..1..0) over phase so the slug bounces. */
        float t01 = p < 0.5f ? p * 2.0f : (1.0f - p) * 2.0f;
        float slug_x = self->x + t01 * travel;
        yetty_ygui_render_ctx_render_box(ctx, slug_x, self->y, slug_w, self->h,
                                         self->accent_color, t->radius_small);
        /* Keep ourselves dirty so we re-emit next frame. */
        self->dirty = 1;
        if (self->engine) self->engine->dirty = 1;
        return YETTY_OK_VOID();
    }

    /* Filled portion */
    float pct = ygui_clamp(self->data.progress.value, 0, 1);
    float fill_w = pct * self->w;
    if (fill_w > 0) {
        yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, fill_w, self->h, self->accent_color,
                                         t->radius_small);
    }
    return YETTY_OK_VOID();
}

struct yetty_ygui_widget *yetty_ygui_engine_progress(struct yetty_ygui_engine *engine,
                                                     const char *id, float x, float y, float w,
                                                     float h, float value)
{
    struct yetty_ygui_widget *prg =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_PROGRESS, id);
    if (!prg) {
        return NULL;
    }

    yetty_ygui_widget_init_base(prg, x, y, w, h);
    prg->data.progress.value = ygui_clamp(value, 0, 1);
    static const struct yetty_ygui_widget_vtable progress_vtable = {
        .render = progress_render,
    };
    prg->vtable = &progress_vtable;

    add_to_engine(engine, prg);
    return prg;
}

void yetty_ygui_widget_progress_set_value(struct yetty_ygui_widget *widget, float value)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_PROGRESS) {
        return;
    }
    widget->data.progress.value = ygui_clamp(value, 0, 1);
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

float yetty_ygui_widget_progress_get_value(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_PROGRESS) {
        return 0;
    }
    return widget->data.progress.value;
}

/*=============================================================================
 * Separator Widget
 *===========================================================================*/

static struct yetty_ycore_void_result separator_render(struct yetty_ygui_widget *self,
                                                       struct yetty_ygui_render_ctx *ctx)
{
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, ctx->theme->border,
                                     0);
    return YETTY_OK_VOID();
}

struct yetty_ygui_widget *yetty_ygui_engine_separator(struct yetty_ygui_engine *engine,
                                                      const char *id, float x, float y, float w,
                                                      float h)
{
    struct yetty_ygui_widget *sep =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_SEPARATOR, id);
    if (!sep) {
        return NULL;
    }

    yetty_ygui_widget_init_base(sep, x, y, w, h);
    static const struct yetty_ygui_widget_vtable separator_vtable = {
        .render = separator_render,
    };
    sep->vtable = &separator_vtable;

    add_to_engine(engine, sep);
    return sep;
}

/*=============================================================================
 * Stub implementations for remaining widgets
 * TODO: Implement fully
 *===========================================================================*/

/*=============================================================================
 * TextInput Widget
 *===========================================================================*/

static struct yetty_ycore_void_result textinput_render(struct yetty_ygui_widget *self,
                                                       struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;

    /* Background */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, self->bg_color,
                                     t->radius_small);

    /* Border - accent if focused, normal otherwise */
    uint32_t border_color =
        (self->flags & YETTY_YGUI_FLAG_FOCUSED) ? self->accent_color : t->border;
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h, border_color,
                                             t->radius_small, 1.5f);

    /* Text or placeholder */
    const char *display_text = self->data.textinput.text;
    uint32_t text_color = self->fg_color;

    if (!display_text || display_text[0] == '\0') {
        display_text = self->data.textinput.placeholder;
        text_color = t->text_muted;
    }

    if (display_text) {
        yetty_ygui_render_ctx_render_text(ctx, display_text, self->x + t->pad_large,
                                          self->y + t->pad_medium, text_color, t->font_size);
    }

    /* Cursor if focused */
    if (self->flags & YETTY_YGUI_FLAG_FOCUSED) {
        float cursor_x = self->x + t->pad_large;
        if (self->data.textinput.text) {
            /* Approximate cursor position based on character count */
            cursor_x += self->data.textinput.cursor_pos * (t->font_size * 0.6f);
        }
        float cursor_y = self->y + t->pad_small;
        float cursor_h = self->h - t->pad_small * 2;
        yetty_ygui_render_ctx_render_box(ctx, cursor_x, cursor_y, 2.0f, cursor_h,
                                         self->accent_color, 0);
    }
    return YETTY_OK_VOID();
}

/* Modifier bits — match yetty's GLFW-derived layout (see yetty/yetty.c,
 * tabbar.c). We only key off CTRL for emacs bindings; SHIFT is already
 * applied at the platform layer when emitting CHAR events. */
#define YGUI_MOD_CTRL  0x0002

/* GLFW key codes for the special keys the textinput needs to react to.
 * Inlining the constants avoids pulling GLFW headers into ygui. */
enum {
    YGUI_KEY_ESCAPE    = 256,
    YGUI_KEY_ENTER     = 257,
    YGUI_KEY_TAB       = 258,
    YGUI_KEY_BACKSPACE = 259,
    YGUI_KEY_DELETE    = 261,
    YGUI_KEY_RIGHT     = 262,
    YGUI_KEY_LEFT      = 263,
    YGUI_KEY_HOME      = 268,
    YGUI_KEY_END       = 269,
    YGUI_KEY_A         = 65,
    YGUI_KEY_B         = 66,
    YGUI_KEY_D         = 68,
    YGUI_KEY_E         = 69,
    YGUI_KEY_F         = 70,
    YGUI_KEY_H         = 72,
    YGUI_KEY_K         = 75,
    YGUI_KEY_U         = 85,
    YGUI_KEY_W         = 87,
};

/* Find the start of the word boundary to the left of `pos`. Used by
 * Ctrl+W (kill-previous-word). Skips trailing whitespace, then runs of
 * non-whitespace — same shape as readline's behaviour. */
static int textinput_word_start(const char *text, int pos)
{
    while (pos > 0 && text[pos - 1] == ' ') {
        pos--;
    }
    while (pos > 0 && text[pos - 1] != ' ') {
        pos--;
    }
    return pos;
}

/* Delete the half-open range [from, to) from `text`, in place. Returns
 * the new length; caller must update cursor afterwards. */
static int textinput_delete_range(char *text, int len, int from, int to)
{
    if (from < 0) from = 0;
    if (to > len) to = len;
    if (from >= to) return len;
    memmove(text + from, text + to, len - to + 1 /* NUL */);
    return len - (to - from);
}

static int textinput_on_key(struct yetty_ygui_widget *self, uint32_t key, int mods,
                            ygui_event_t *out)
{
    char *text = self->data.textinput.text;
    int len = text ? (int)strlen(text) : 0;
    int cursor = self->data.textinput.cursor_pos;
    if (cursor < 0) cursor = 0;
    if (cursor > len) cursor = len;
    int handled = 0;

    int ctrl = (mods & YGUI_MOD_CTRL) ? 1 : 0;

    /* Printable text DOES NOT come through here — the platform emits a
     * separate CHAR event (yetty_ygui_engine_text_input). KEY_DOWN is
     * for navigation / editing commands only. Mixing both was the
     * "every letter appears twice, once upper, once lower" bug.
     *
     * We treat Ctrl+letter as an emacs-style chord. ASCII letter keys
     * land here as GLFW codes 65..90 (always uppercase) with mods set;
     * the chord is unambiguous regardless of the shift state. */
    if (ctrl) {
        switch (key) {
        case YGUI_KEY_A:                /* go to start of line */
            cursor = 0; handled = 1; break;
        case YGUI_KEY_E:                /* go to end of line */
            cursor = len; handled = 1; break;
        case YGUI_KEY_B:                /* back one char */
            if (cursor > 0) cursor--;
            handled = 1; break;
        case YGUI_KEY_F:                /* forward one char */
            if (cursor < len) cursor++;
            handled = 1; break;
        case YGUI_KEY_H:                /* backspace */
            if (text && cursor > 0) {
                len = textinput_delete_range(text, len, cursor - 1, cursor);
                cursor--;
            }
            handled = 1; break;
        case YGUI_KEY_D:                /* delete-char-forward */
            if (text && cursor < len) {
                len = textinput_delete_range(text, len, cursor, cursor + 1);
            }
            handled = 1; break;
        case YGUI_KEY_K:                /* kill to end-of-line */
            if (text && cursor < len) {
                len = textinput_delete_range(text, len, cursor, len);
            }
            handled = 1; break;
        case YGUI_KEY_U:                /* kill to start-of-line */
            if (text && cursor > 0) {
                len = textinput_delete_range(text, len, 0, cursor);
                cursor = 0;
            }
            handled = 1; break;
        case YGUI_KEY_W: {              /* kill previous word */
            if (text && cursor > 0) {
                int ws = textinput_word_start(text, cursor);
                len = textinput_delete_range(text, len, ws, cursor);
                cursor = ws;
            }
            handled = 1; break;
        }
        }
    } else {
        switch (key) {
        case YGUI_KEY_BACKSPACE:
        case 8:                          /* ASCII BS (some platforms) */
        case 127:                        /* ASCII DEL (some platforms) */
            if (text && cursor > 0) {
                len = textinput_delete_range(text, len, cursor - 1, cursor);
                cursor--;
            }
            handled = 1; break;
        case YGUI_KEY_DELETE:
            if (text && cursor < len) {
                len = textinput_delete_range(text, len, cursor, cursor + 1);
            }
            handled = 1; break;
        case YGUI_KEY_LEFT:
            if (cursor > 0) cursor--;
            handled = 1; break;
        case YGUI_KEY_RIGHT:
            if (cursor < len) cursor++;
            handled = 1; break;
        case YGUI_KEY_HOME:
            cursor = 0; handled = 1; break;
        case YGUI_KEY_END:
            cursor = len; handled = 1; break;
        }
    }

    if (!handled) {
        return 0;
    }

    self->data.textinput.cursor_pos = cursor;

    if (self->text_callback) {
        self->text_callback(self, text ? text : "", self->text_userdata);
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.string_value = text ? text : "";
    return 1;
}

static void textinput_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.textinput.text);
    free(self->data.textinput.placeholder);
}

struct yetty_ygui_widget *yetty_ygui_engine_textinput(struct yetty_ygui_engine *engine,
                                                      const char *id, float x, float y, float w,
                                                      float h, const char *placeholder)
{
    struct yetty_ygui_widget *txt =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_TEXTINPUT, id);
    if (!txt) {
        return NULL;
    }

    yetty_ygui_widget_init_base(txt, x, y, w, h);
    txt->data.textinput.text = ygui_strdup("");
    txt->data.textinput.placeholder = ygui_strdup(placeholder);
    txt->data.textinput.cursor_pos = 0;
    static const struct yetty_ygui_widget_vtable textinput_vtable = {
        .render = textinput_render,
        .on_key = textinput_on_key,
        .destroy = textinput_destroy,
    };
    txt->vtable = &textinput_vtable;

    add_to_engine(engine, txt);
    return txt;
}

void yetty_ygui_widget_textinput_set_text(struct yetty_ygui_widget *widget, const char *text)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TEXTINPUT) {
        return;
    }
    free(widget->data.textinput.text);
    widget->data.textinput.text = ygui_strdup(text);
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

const char *yetty_ygui_widget_textinput_get_text(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TEXTINPUT) {
        return NULL;
    }
    return widget->data.textinput.text;
}

void yetty_ygui_widget_textinput_set_placeholder(struct yetty_ygui_widget *widget, const char *text)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TEXTINPUT) {
        return;
    }
    free(widget->data.textinput.placeholder);
    widget->data.textinput.placeholder = ygui_strdup(text);
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

/*=============================================================================
 * HBox / VBox — flex containers (row / column).
 *
 * Layout is computed in ygui_layout.c; rendering reuses the default
 * render_all. Theme padding/gap are applied at construction time so
 * existing callers see the same visual behavior they did before.
 *===========================================================================*/

static void box_apply_theme_layout(struct yetty_ygui_widget *box,
                                   const struct yetty_ygui_theme *theme,
                                   ygui_flex_direction_t direction)
{
    box->layout.mode = YETTY_YGUI_LAYOUT_FLEX;
    box->layout.direction = direction;
    box->layout.gap = theme->pad_medium;
    box->layout.padding_top = theme->pad_medium;
    box->layout.padding_right = theme->pad_medium;
    box->layout.padding_bottom = theme->pad_medium;
    box->layout.padding_left = theme->pad_medium;
}

struct yetty_ygui_widget *yetty_ygui_engine_hbox(struct yetty_ygui_engine *engine, const char *id,
                                                 float x, float y, float w, float h)
{
    struct yetty_ygui_widget *hbox =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_HBOX, id);
    if (!hbox) {
        return NULL;
    }
    yetty_ygui_widget_init_base(hbox, x, y, w, h);
    box_apply_theme_layout(hbox, engine->theme, YETTY_YGUI_FLEX_ROW);
    add_to_engine(engine, hbox);
    return hbox;
}

struct yetty_ygui_widget *yetty_ygui_engine_vbox(struct yetty_ygui_engine *engine, const char *id,
                                                 float x, float y, float w, float h)
{
    struct yetty_ygui_widget *vbox =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_VBOX, id);
    if (!vbox) {
        return NULL;
    }
    yetty_ygui_widget_init_base(vbox, x, y, w, h);
    box_apply_theme_layout(vbox, engine->theme, YETTY_YGUI_FLEX_COLUMN);
    add_to_engine(engine, vbox);
    return vbox;
}

/*=============================================================================
 * Dropdown Widget
 *===========================================================================*/

static struct yetty_ycore_void_result dropdown_render(struct yetty_ygui_widget *self,
                                                      struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    int is_open = self->data.dropdown.open;

    /* Low elevation when closed; the open list itself takes medium below. */
    yetty_ygui_render_ctx_render_box_shadow(ctx, self->x, self->y, self->w, self->h,
                                            t->radius_medium, t->elevation_low, t->shadow,
                                            t->elevation_alpha);

    /* Main button area */
    uint32_t bg = (self->flags & YETTY_YGUI_FLAG_HOVER) ? t->bg_hover : self->bg_color;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, bg, t->radius_medium);
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h, t->border,
                                             t->radius_medium, 1.0f);

    /* Selected text */
    const char *selected_text = NULL;
    if (self->data.dropdown.options && self->data.dropdown.selected >= 0 &&
        self->data.dropdown.selected < self->data.dropdown.option_count) {
        selected_text = self->data.dropdown.options[self->data.dropdown.selected];
    }
    if (selected_text) {
        yetty_ygui_render_ctx_render_text(ctx, selected_text, self->x + t->pad_large,
                                          self->y + t->pad_medium, self->fg_color, t->font_size);
    }

    /* Arrow indicator */
    float arrow_x = self->x + self->w - t->pad_large - 8;
    float arrow_y = self->y + self->h / 2;
    if (is_open) {
        /* Up arrow */
        yetty_ygui_render_ctx_render_triangle(ctx, arrow_x, arrow_y + 3, arrow_x + 8, arrow_y + 3,
                                              arrow_x + 4, arrow_y - 3, self->fg_color);
    } else {
        /* Down arrow */
        yetty_ygui_render_ctx_render_triangle(ctx, arrow_x, arrow_y - 3, arrow_x + 8, arrow_y - 3,
                                              arrow_x + 4, arrow_y + 3, self->fg_color);
    }

    /* Dropdown list when open */
    if (is_open && self->data.dropdown.options) {
        float list_y = self->y + self->h + 2;
        float item_h = t->row_height;
        float list_h = self->data.dropdown.option_count * item_h;

        /* List background */
        yetty_ygui_render_ctx_render_box(ctx, self->x, list_y, self->w, list_h, t->bg_surface,
                                         t->radius_medium);
        yetty_ygui_render_ctx_render_box_outline(ctx, self->x, list_y, self->w, list_h, t->border,
                                                 t->radius_medium, 1.0f);

        /* Options */
        for (int i = 0; i < self->data.dropdown.option_count; i++) {
            float opt_y = list_y + i * item_h;
            if (i == self->data.dropdown.selected) {
                yetty_ygui_render_ctx_render_box(ctx, self->x + 2, opt_y + 2, self->w - 4,
                                                 item_h - 4, self->accent_color, t->radius_small);
            }
            yetty_ygui_render_ctx_render_text(ctx, self->data.dropdown.options[i],
                                              self->x + t->pad_large, opt_y + t->pad_small,
                                              self->fg_color, t->font_size);
        }
    }
    return YETTY_OK_VOID();
}

static int dropdown_on_release(struct yetty_ygui_widget *self, float lx, float ly,
                               ygui_event_t *out)
{
    const struct yetty_ygui_theme *t = self->engine->theme;

    if (self->data.dropdown.open) {
        /* Check if clicked on an option */
        float list_y_start = self->h + 2;
        float item_h = t->row_height;

        if (ly >= list_y_start) {
            int idx = (int)((ly - list_y_start) / item_h);
            if (idx >= 0 && idx < self->data.dropdown.option_count) {
                self->data.dropdown.selected = idx;
                out->widget_id = self->id;
                out->type = YETTY_YGUI_EVENT_CHANGE;
                out->data.int_value = idx;
            }
        }
        self->data.dropdown.open = 0;
    } else {
        /* Toggle open */
        if (lx >= 0 && lx < self->w && ly >= 0 && ly < self->h) {
            self->data.dropdown.open = 1;
        }
    }
    return 1;
}

static void dropdown_free_options(struct yetty_ygui_widget *self)
{
    if (self->data.dropdown.options) {
        for (int i = 0; i < self->data.dropdown.option_count; i++) {
            free(self->data.dropdown.options[i]);
        }
        free(self->data.dropdown.options);
        self->data.dropdown.options = NULL;
    }
}

static void dropdown_copy_options(struct yetty_ygui_widget *self, const char **options, int count)
{
    dropdown_free_options(self);
    if (!options || count <= 0) {
        self->data.dropdown.option_count = 0;
        return;
    }
    self->data.dropdown.options = (char **)malloc(count * sizeof(char *));
    if (!self->data.dropdown.options) {
        self->data.dropdown.option_count = 0;
        return;
    }
    for (int i = 0; i < count; i++) {
        self->data.dropdown.options[i] = ygui_strdup(options[i]);
    }
    self->data.dropdown.option_count = count;
}

static void dropdown_destroy(struct yetty_ygui_widget *self)
{
    dropdown_free_options(self);
}

struct yetty_ygui_widget *yetty_ygui_engine_dropdown(struct yetty_ygui_engine *engine,
                                                     const char *id, float x, float y, float w,
                                                     float h, const char **options,
                                                     int option_count)
{
    struct yetty_ygui_widget *dd =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_DROPDOWN, id);
    if (!dd) {
        return NULL;
    }
    yetty_ygui_widget_init_base(dd, x, y, w, h);
    dd->data.dropdown.options = NULL;
    dd->data.dropdown.option_count = 0;
    dd->data.dropdown.selected = 0;
    dd->data.dropdown.open = 0;
    dropdown_copy_options(dd, options, option_count);
    static const struct yetty_ygui_widget_vtable dropdown_vtable = {
        .render = dropdown_render,
        .on_release = dropdown_on_release,
        .destroy = dropdown_destroy,
    };
    dd->vtable = &dropdown_vtable;
    add_to_engine(engine, dd);
    return dd;
}

void yetty_ygui_widget_dropdown_set_options(struct yetty_ygui_widget *widget, const char **options,
                                            int count)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_DROPDOWN) {
        return;
    }
    dropdown_copy_options(widget, options, count);
    if (widget->data.dropdown.selected >= count) {
        widget->data.dropdown.selected = count > 0 ? 0 : -1;
    }
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

void yetty_ygui_widget_dropdown_set_selected(struct yetty_ygui_widget *widget, int index)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_DROPDOWN) {
        return;
    }
    widget->data.dropdown.selected = index;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

int yetty_ygui_widget_dropdown_get_selected(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_DROPDOWN) {
        return 0;
    }
    return widget->data.dropdown.selected;
}

/*=============================================================================
 * ColorPicker Widget
 *===========================================================================*/

/* HSV to RGB conversion */
static void hsv_to_rgb(float h, float s, float v, float *r, float *g, float *b)
{
    if (s == 0) {
        *r = *g = *b = v;
        return;
    }
    h = h - (int)h; /* Wrap to 0-1 */
    if (h < 0) {
        h += 1;
    }
    h *= 6.0f;
    int i = (int)h;
    float f = h - i;
    float p = v * (1 - s);
    float q = v * (1 - s * f);
    float t = v * (1 - s * (1 - f));
    switch (i % 6) {
    case 0:
        *r = v;
        *g = t;
        *b = p;
        break;
    case 1:
        *r = q;
        *g = v;
        *b = p;
        break;
    case 2:
        *r = p;
        *g = v;
        *b = t;
        break;
    case 3:
        *r = p;
        *g = q;
        *b = v;
        break;
    case 4:
        *r = t;
        *g = p;
        *b = v;
        break;
    case 5:
        *r = v;
        *g = p;
        *b = q;
        break;
    }
}

/* RGB to HSV conversion */
static void rgb_to_hsv(float r, float g, float b, float *h, float *s, float *v)
{
    float max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d = max - min;
    *v = max;
    *s = (max == 0) ? 0 : d / max;
    if (d == 0) {
        *h = 0;
    } else if (max == r) {
        *h = (g - b) / d / 6.0f;
        if (*h < 0) {
            *h += 1;
        }
    } else if (max == g) {
        *h = ((b - r) / d + 2) / 6.0f;
    } else {
        *h = ((r - g) / d + 4) / 6.0f;
    }
}

static uint32_t make_color_abgr(float r, float g, float b, float a)
{
    uint8_t ri = (uint8_t)(r * 255);
    uint8_t gi = (uint8_t)(g * 255);
    uint8_t bi = (uint8_t)(b * 255);
    uint8_t ai = (uint8_t)(a * 255);
    return ((uint32_t)ai << 24) | ((uint32_t)bi << 16) | ((uint32_t)gi << 8) | ri;
}

static struct yetty_ycore_void_result colorpicker_render(struct yetty_ygui_widget *self,
                                                         struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float hue = self->data.colorpicker.hue;
    float sat = self->data.colorpicker.sat;
    float val = self->data.colorpicker.val;

    /* Layout: SV gradient on top, hue slider below, preview box on right */
    float hue_bar_h = 20.0f;
    float preview_w = 40.0f;
    float sv_w = self->w - preview_w - t->pad_medium;
    float sv_h = self->h - hue_bar_h - t->pad_medium;

    /* SV gradient area - use color wheel primitive */
    float r, g, b;
    hsv_to_rgb(hue, 1.0f, 1.0f, &r, &g, &b);
    uint32_t hue_color = make_color_abgr(r, g, b, 1.0f);

    /* Background with current hue */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, sv_w, sv_h, hue_color, t->radius_small);

    /* SV indicator */
    float ind_x = self->x + sat * sv_w;
    float ind_y = self->y + (1 - val) * sv_h;
    yetty_ygui_render_ctx_render_circle(ctx, ind_x, ind_y, 6.0f, 0xFFFFFFFF);
    yetty_ygui_render_ctx_render_circle(ctx, ind_x, ind_y, 4.0f, 0xFF000000);

    /* Hue slider bar */
    float hue_y = self->y + sv_h + t->pad_medium;
    yetty_ygui_render_ctx_render_box(ctx, self->x, hue_y, sv_w, hue_bar_h, t->bg_surface,
                                     t->radius_small);

    /* Hue indicator */
    float hue_ind_x = self->x + hue * sv_w;
    yetty_ygui_render_ctx_render_box(ctx, hue_ind_x - 3, hue_y, 6, hue_bar_h, 0xFFFFFFFF,
                                     t->radius_small);

    /* Color preview */
    float preview_x = self->x + sv_w + t->pad_medium;
    hsv_to_rgb(hue, sat, val, &r, &g, &b);
    uint32_t preview_color = make_color_abgr(r, g, b, self->data.colorpicker.alpha);
    yetty_ygui_render_ctx_render_box(ctx, preview_x, self->y, preview_w, self->h, preview_color,
                                     t->radius_medium);
    yetty_ygui_render_ctx_render_box_outline(ctx, preview_x, self->y, preview_w, self->h, t->border,
                                             t->radius_medium, 1.5f);
    return YETTY_OK_VOID();
}

static int colorpicker_on_press(struct yetty_ygui_widget *self, float lx, float ly,
                                ygui_event_t *out)
{
    const struct yetty_ygui_theme *t = self->engine->theme;
    float hue_bar_h = 20.0f;
    float preview_w = 40.0f;
    float sv_w = self->w - preview_w - t->pad_medium;
    float sv_h = self->h - hue_bar_h - t->pad_medium;

    if (ly < sv_h && lx < sv_w) {
        /* Clicked in SV area */
        self->data.colorpicker.sat = ygui_clamp(lx / sv_w, 0, 1);
        self->data.colorpicker.val = ygui_clamp(1 - ly / sv_h, 0, 1);
    } else if (ly >= sv_h + t->pad_medium && lx < sv_w) {
        /* Clicked in hue bar */
        self->data.colorpicker.hue = ygui_clamp(lx / sv_w, 0, 1);
    }

    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    return 1;
}

static int colorpicker_on_drag(struct yetty_ygui_widget *self, float lx, float ly,
                               ygui_event_t *out)
{
    return colorpicker_on_press(self, lx, ly, out);
}

struct yetty_ygui_widget *yetty_ygui_engine_colorpicker(struct yetty_ygui_engine *engine,
                                                        const char *id, float x, float y, float w,
                                                        float h)
{
    struct yetty_ygui_widget *cp =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_COLORPICKER, id);
    if (!cp) {
        return NULL;
    }
    yetty_ygui_widget_init_base(cp, x, y, w, h);
    cp->data.colorpicker.hue = 0;
    cp->data.colorpicker.sat = 1;
    cp->data.colorpicker.val = 1;
    cp->data.colorpicker.alpha = 1;
    static const struct yetty_ygui_widget_vtable colorpicker_vtable = {
        .render = colorpicker_render,
        .on_press = colorpicker_on_press,
        .on_drag = colorpicker_on_drag,
    };
    cp->vtable = &colorpicker_vtable;
    add_to_engine(engine, cp);
    return cp;
}

void yetty_ygui_widget_colorpicker_set_color(struct yetty_ygui_widget *widget, float r, float g,
                                             float b, float a)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COLORPICKER) {
        return;
    }
    rgb_to_hsv(r, g, b, &widget->data.colorpicker.hue, &widget->data.colorpicker.sat,
               &widget->data.colorpicker.val);
    widget->data.colorpicker.alpha = a;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

void yetty_ygui_widget_colorpicker_get_color(const struct yetty_ygui_widget *widget, float *r,
                                             float *g, float *b, float *a)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COLORPICKER) {
        if (r) {
            *r = 1;
        }
        if (g) {
            *g = 1;
        }
        if (b) {
            *b = 1;
        }
        if (a) {
            *a = 1;
        }
        return;
    }
    float ri, gi, bi;
    hsv_to_rgb(widget->data.colorpicker.hue, widget->data.colorpicker.sat,
               widget->data.colorpicker.val, &ri, &gi, &bi);
    if (r) {
        *r = ri;
    }
    if (g) {
        *g = gi;
    }
    if (b) {
        *b = bi;
    }
    if (a) {
        *a = widget->data.colorpicker.alpha;
    }
}

/*=============================================================================
 * Popup Widget
 *
 * Modal/non-modal popup window with optional header. Children only render
 * when the popup is open. Press toggles open state.
 *===========================================================================*/

/* Title bar height for drag + close-button hit testing. Mirrors the
 * value popup_render uses to paint the header strip. Always non-zero
 * (even on popups with NULL label) so the drag affordance still works
 * — labelless popups get an invisible drag handle along the top edge. */
static float popup_title_h(const struct yetty_ygui_theme *t)
{
    float h = t->row_height + t->pad_medium;
    if (h < 24.0f) h = 24.0f;
    return h;
}

#define POPUP_CLOSE_BTN_SIZE 18.0f
#define POPUP_CLOSE_BTN_PAD  6.0f

static struct yetty_ycore_void_result popup_render(struct yetty_ygui_widget *self,
                                                   struct yetty_ygui_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_FLAG_OPEN)) {
        return YETTY_OK_VOID();
    }
    const struct yetty_ygui_theme *t = ctx->theme;

    if (self->data.popup.modal) {
        yetty_ygui_render_ctx_render_box(ctx, 0, 0, self->data.popup.scene_w,
                                         self->data.popup.scene_h, t->overlay_modal, 0);
    }

    /* Soft drop shadow (high elevation — popups float above everything). */
    yetty_ygui_render_ctx_render_box_shadow(ctx, self->x, self->y, self->w, self->h,
                                            t->radius_large, t->elevation_high, t->shadow,
                                            t->elevation_alpha);

    /* Body + outline */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, self->bg_color,
                                     t->radius_large);
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h,
                                             self->accent_color, t->radius_large, 2.0f);

    /* Header strip — always painted so the drag area is visible. */
    float hdr_h = popup_title_h(t);
    uint32_t hdr = self->data.popup.header_color ? self->data.popup.header_color : t->bg_header;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, hdr_h, hdr,
                                     t->radius_large);
    const char *label = self->data.popup.label;
    if (label && label[0]) {
        yetty_ygui_render_ctx_render_text(ctx, label, self->x + t->pad_large,
                                          self->y + t->pad_large - 2, self->fg_color, t->font_size);
    }
    /* Close button (×) at top-right of the title bar. */
    float cb_x = self->x + self->w - POPUP_CLOSE_BTN_SIZE - POPUP_CLOSE_BTN_PAD;
    float cb_y = self->y + (hdr_h - POPUP_CLOSE_BTN_SIZE) * 0.5f;
    yetty_ygui_render_ctx_render_box(ctx, cb_x, cb_y, POPUP_CLOSE_BTN_SIZE, POPUP_CLOSE_BTN_SIZE,
                                     t->bg_secondary, POPUP_CLOSE_BTN_SIZE * 0.2f);
    /* Two crossed lines for the × glyph. */
    float gx = cb_x + POPUP_CLOSE_BTN_SIZE * 0.5f;
    float gy = cb_y + POPUP_CLOSE_BTN_SIZE * 0.5f;
    float g = POPUP_CLOSE_BTN_SIZE * 0.30f;
    yetty_ygui_render_ctx_render_box(ctx, gx - g, gy - 1.0f, g * 2, 2.0f, self->fg_color, 1);
    yetty_ygui_render_ctx_render_box(ctx, gx - 1.0f, gy - g, 2.0f, g * 2, self->fg_color, 1);
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result popup_render_all(struct yetty_ygui_widget *self,
                                                       struct yetty_ygui_render_ctx *ctx)
{
    /* When closed: bail out completely. Earlier this function still
     * set was_rendered = 1 even with OPEN=0, which kept the popup
     * registered in the engine's spatial grid at its rect. The result
     * was "ghost clicks": after the popup closed, any click in its
     * former area routed back to popup_on_press, which toggled OPEN
     * to 1 and made the dialog reappear. Returning early here keeps
     * the popup out of the grid entirely when it's closed. */
    if (!(self->flags & YETTY_YGUI_FLAG_OPEN)) {
        return YETTY_OK_VOID();
    }

    self->effective_x = self->x + ctx->offset_x;
    self->effective_y = self->y + ctx->offset_y;
    self->was_rendered = 1;

    uint32_t marker = yetty_ygui_widget_open_group(self, ctx, popup_render);

    for (struct yetty_ygui_widget *child = self->first_child; child;
         child = child->next_sibling) {
        if (child->vtable && child->vtable->render_all) {
            child->vtable->render_all(child, ctx);
        } else {
            yetty_ygui_widget_render_all_default(child, ctx);
        }
    }

    yetty_ygui_widget_close_group(self, ctx, marker);
    return YETTY_OK_VOID();
}

/* Returns 1 if (lx, ly) falls inside the close-button × glyph rect. */
static int popup_hit_close_btn(const struct yetty_ygui_widget *self, float lx, float ly,
                               float hdr_h)
{
    float cb_x = self->w - POPUP_CLOSE_BTN_SIZE - POPUP_CLOSE_BTN_PAD;
    float cb_y = (hdr_h - POPUP_CLOSE_BTN_SIZE) * 0.5f;
    return (lx >= cb_x && lx < cb_x + POPUP_CLOSE_BTN_SIZE && ly >= cb_y &&
            ly < cb_y + POPUP_CLOSE_BTN_SIZE);
}

static int popup_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    const struct yetty_ygui_theme *t = self->engine ? self->engine->theme : NULL;
    float hdr_h = t ? popup_title_h(t) : 28.0f;

    /* Close button — always close, modal or not. */
    if (ly >= 0 && ly < hdr_h && popup_hit_close_btn(self, lx, ly, hdr_h)) {
        int was_open = (self->flags & YETTY_YGUI_FLAG_OPEN) != 0;
        self->flags &= ~YETTY_YGUI_FLAG_OPEN;
        if (self->engine) {
            self->engine->dirty = 1; self->dirty = 1;
            if (was_open) {
                yetty_ygui_internal_queue_delete_subtree_rendered(self);
            }
        }
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_CLICK;
        out->data.bool_value = 0;
        return 1;
    }

    /* Title-bar drag — modal AND non-modal. Save ABSOLUTE mouse coords
     * at press so on_drag's delta math is invariant to layout updates
     * (the engine recomputes `lx = mouse - effective_x` every move,
     * and effective_x changes as soon as we move the widget). Using
     * widget-local lx as the anchor produces a one-frame lag that
     * shows up as flicker / drift behind the cursor. */
    if (ly >= 0 && ly < hdr_h) {
        self->data.popup.dragging = 1;
        self->data.popup.drag_press_lx = lx + self->effective_x;
        self->data.popup.drag_press_ly = ly + self->effective_y;
        self->data.popup.drag_orig_x = self->x;
        self->data.popup.drag_orig_y = self->y;
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_PRESS;
        return 1;
    }

    /* Body click — consume so it doesn't leak to whatever is rendered
     * behind. Modal AND non-modal: no auto-close here (close via the ×
     * button or programmatically via popup_set_open). The earlier
     * "non-modal closes on any body click" behaviour was confusing —
     * users expected to be able to interact with popup content. */
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_PRESS;
    return 1;
}

static int popup_on_drag(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    if (!self->data.popup.dragging) return 0;
    /* `lx + effective_x` is the invariant: it always equals the
     * absolute mouse_x regardless of any intervening layout that
     * updated effective_x (see comment in popup_on_press). Using that
     * recovered absolute coord as the delta source keeps the widget
     * pinned under the cursor. */
    float mouse_x = lx + self->effective_x;
    float mouse_y = ly + self->effective_y;
    float dx = mouse_x - self->data.popup.drag_press_lx;
    float dy = mouse_y - self->data.popup.drag_press_ly;
    self->x = self->data.popup.drag_orig_x + dx;
    self->y = self->data.popup.drag_orig_y + dy;
    self->authored_x = self->x;
    self->authored_y = self->y;
    self->dirty = 1;
    if (self->engine) self->engine->dirty = 1;
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    return 1;
}

static int popup_on_release(struct yetty_ygui_widget *self, float lx, float ly,
                            ygui_event_t *out)
{
    (void)lx; (void)ly; (void)out;
    self->data.popup.dragging = 0;
    return 0;
}

static void popup_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.popup.label);
}

struct yetty_ygui_widget *yetty_ygui_engine_popup(struct yetty_ygui_engine *engine, const char *id,
                                                  float x, float y, float w, float h,
                                                  const char *label)
{
    struct yetty_ygui_widget *p =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_POPUP, id);
    if (!p) {
        return NULL;
    }
    yetty_ygui_widget_init_base(p, x, y, w, h);
    p->data.popup.label = ygui_strdup(label);
    p->data.popup.modal = 0;
    p->data.popup.header_color = 0;
    p->data.popup.scene_w = engine->width;
    p->data.popup.scene_h = engine->height;
    static const struct yetty_ygui_widget_vtable popup_vtable = {
        .render = popup_render,
        .render_all = popup_render_all,
        .on_press = popup_on_press,
        .on_drag = popup_on_drag,
        .on_release = popup_on_release,
        .destroy = popup_destroy,
    };
    p->vtable = &popup_vtable;
    add_to_engine(engine, p);
    return p;
}

void yetty_ygui_widget_popup_set_label(struct yetty_ygui_widget *widget, const char *label)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return;
    }
    free(widget->data.popup.label);
    widget->data.popup.label = ygui_strdup(label);
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

const char *yetty_ygui_widget_popup_get_label(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return NULL;
    }
    return widget->data.popup.label;
}

void yetty_ygui_widget_popup_set_modal(struct yetty_ygui_widget *widget, int modal)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return;
    }
    widget->data.popup.modal = modal ? 1 : 0;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

int yetty_ygui_widget_popup_is_modal(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return 0;
    }
    return widget->data.popup.modal;
}

void yetty_ygui_widget_popup_set_open(struct yetty_ygui_widget *widget, int open)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return;
    }
    int was_open = (widget->flags & YETTY_YGUI_FLAG_OPEN) != 0;
    if (open) {
        widget->flags |= YETTY_YGUI_FLAG_OPEN;
        /* Float above every widget painted earlier this frame so the
         * popup is never occluded by a sibling later in the engine's
         * widget chain. */
        if (!was_open) {
            yetty_ygui_internal_bring_to_front(widget);
        }
    } else {
        widget->flags &= ~YETTY_YGUI_FLAG_OPEN;
    }
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
        /* OPEN → close: popup_render_all early-returns for the rest of
         * the subtree, so the children's entities on the receiver would
         * never get a DELETE through emit_self_in_group. Queue them. */
        if (was_open && !open) {
            yetty_ygui_internal_queue_delete_subtree_rendered(widget);
        }
    }
}

int yetty_ygui_widget_popup_is_open(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return 0;
    }
    return (widget->flags & YETTY_YGUI_FLAG_OPEN) ? 1 : 0;
}

void yetty_ygui_widget_popup_set_scene_size(struct yetty_ygui_widget *widget, float w, float h)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return;
    }
    widget->data.popup.scene_w = w;
    widget->data.popup.scene_h = h;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

void yetty_ygui_widget_popup_set_header_color(struct yetty_ygui_widget *widget, uint32_t color)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_POPUP) {
        return;
    }
    widget->data.popup.header_color = color;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

/*=============================================================================
 * CollapsingHeader Widget
 *
 * Header bar with arrow + label; toggles open on press; when open, lays its
 * children out vertically below the header.
 *===========================================================================*/

static struct yetty_ycore_void_result collapsing_header_render(struct yetty_ygui_widget *self,
                                                               struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    /* `self->h` now reports the full expanded box (header strip +
     * children) so the parent's flex layout can stack siblings
     * properly. The header BAR — bg fill, chevron, label text, hover
     * outline — must paint only over the strip itself, so we use
     * `header_h` instead of `self->h`. */
    float header_h = self->data.collapsing_header.header_h;
    if (header_h <= 0.0f) {
        header_h = self->h;
    }

    /* When OPEN, draw a rounded body frame behind the children so the
     * section is visually grouped (otherwise children just flow below
     * the header and it's unclear what belongs to which section). */
    if ((self->flags & YETTY_YGUI_FLAG_OPEN) && self->h > header_h + 0.5f) {
        float body_y = self->y + header_h;
        float body_h = self->h - header_h;
        yetty_ygui_render_ctx_render_box(ctx, self->x, body_y, self->w, body_h, t->bg_secondary,
                                         t->radius_medium);
        yetty_ygui_render_ctx_render_box_outline(ctx, self->x, body_y, self->w, body_h,
                                                 t->border_muted, t->radius_medium, 1.0f);
    }

    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, header_h, self->bg_color,
                                     t->radius_medium);

    float arrow_size = t->pad_large;
    float arrow_x = self->x + t->pad_large + 2;
    float arrow_y = self->y + header_h * 0.5f;
    if (self->flags & YETTY_YGUI_FLAG_OPEN) {
        /* Down-pointing triangle (V) — reminds the user the section is
         * expanded. */
        yetty_ygui_render_ctx_render_triangle(ctx, arrow_x, arrow_y - arrow_size / 3.0f,
                                              arrow_x + arrow_size, arrow_y - arrow_size / 3.0f,
                                              arrow_x + arrow_size / 2, arrow_y + arrow_size / 3.0f,
                                              self->fg_color);
    } else {
        /* Right-pointing triangle (>) — section is collapsed. */
        yetty_ygui_render_ctx_render_triangle(ctx, arrow_x, arrow_y - arrow_size / 2.0f, arrow_x,
                                              arrow_y + arrow_size / 2.0f,
                                              arrow_x + arrow_size * 0.7f, arrow_y, self->fg_color);
    }

    if (self->data.collapsing_header.label) {
        yetty_ygui_render_ctx_render_text(ctx, self->data.collapsing_header.label,
                                          self->x + arrow_size + t->pad_large * 2 + 2,
                                          self->y + t->pad_medium, self->fg_color, t->font_size);
    }
    if (self->flags & YETTY_YGUI_FLAG_HOVER) {
        yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, header_h,
                                                 self->accent_color, t->radius_medium, 1.5f);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result collapsing_header_render_all(
    struct yetty_ygui_widget *self, struct yetty_ygui_render_ctx *ctx)
{
    self->effective_x = self->x + ctx->offset_x;
    self->effective_y = self->y + ctx->offset_y;
    self->was_rendered = 1;

    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();

    /* Open this widget's GROUP, emit the header strip body, then
     * recurse children INSIDE the open scope (so they nest properly
     * on the wire). Children are skipped when the collapser is closed. */
    uint32_t marker = yetty_ygui_widget_open_group(self, ctx, collapsing_header_render);

    if ((self->flags & YETTY_YGUI_FLAG_OPEN) && self->first_child) {
        /* Children were positioned by the flex pass into the box just
         * below the header (padding_top = header_h). Their stored
         * (x, y) is already parent-relative — no ctx offset needed. */
        for (struct yetty_ygui_widget *child = self->first_child; child;
             child = child->next_sibling) {
            if (!(child->flags & YETTY_YGUI_FLAG_VISIBLE)) {
                continue;
            }
            struct yetty_ycore_void_result r;
            if (child->vtable && child->vtable->render_all) {
                r = child->vtable->render_all(child, ctx);
            } else {
                r = yetty_ygui_widget_render_all_default(child, ctx);
            }
            if (YETTY_IS_ERR(r)) {
                if (YETTY_IS_OK(first_err)) {
                    first_err = r;
                } else {
                    yetty_ycore_error_destroy(r.error);
                }
            }
        }
    }

    yetty_ygui_widget_close_group(self, ctx, marker);
    return first_err;
}

static int collapsing_header_on_press(struct yetty_ygui_widget *self, float lx, float ly,
                                      ygui_event_t *out)
{
    (void)lx;
    /* `self->h` now spans the full expanded box; only the top `header_h`
     * strip should toggle. A click below the strip lands on a child
     * widget — let the framework dispatch it to that child instead of
     * accidentally collapsing the section. Return 0 so the press
     * propagates through to the deeper hit. */
    float header_h = self->data.collapsing_header.header_h;
    if (header_h > 0.0f && ly >= header_h) {
        return 0;
    }
    int was_open = (self->flags & YETTY_YGUI_FLAG_OPEN) != 0;
    self->flags ^= YETTY_YGUI_FLAG_OPEN;
    int now_open = (self->flags & YETTY_YGUI_FLAG_OPEN) != 0;
    if (self->engine) {
        self->engine->dirty = 1; self->dirty = 1;
        if (was_open && !now_open) {
            /* Header keeps rendering; only children stopped. */
            for (struct yetty_ygui_widget *c = self->first_child; c; c = c->next_sibling) {
                yetty_ygui_internal_queue_delete_subtree_rendered(c);
            }
        }
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CLICK;
    out->data.bool_value = now_open ? 1 : 0;
    return 1;
}

static void collapsing_header_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.collapsing_header.label);
}

struct yetty_ygui_widget *yetty_ygui_engine_collapsing_header(struct yetty_ygui_engine *engine,
                                                              const char *id, float x, float y,
                                                              float w, float h, const char *label)
{
    struct yetty_ygui_widget *c =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_COLLAPSING_HEADER, id);
    if (!c) {
        return NULL;
    }
    yetty_ygui_widget_init_base(c, x, y, w, h);
    c->data.collapsing_header.label = ygui_strdup(label);
    c->data.collapsing_header.header_h = h;
    /* Use flex-column for children — the preflight resizes our authored_h
     * to header_h + children when OPEN, so siblings stacked above us in
     * the parent's flex pass see the real expanded height (no overlap).
     * padding_top = header_h keeps children below the header strip; the
     * default ygreeter-style gap of 4 lines the rows up cleanly. */
    c->layout.mode = YETTY_YGUI_LAYOUT_FLEX;
    c->layout.direction = YETTY_YGUI_FLEX_COLUMN;
    c->layout.align_items = YETTY_YGUI_ALIGN_STRETCH;
    c->layout.padding_top = h + 8.0f;   /* header strip + inset above first child */
    c->layout.padding_bottom = 8.0f;
    c->layout.padding_left = 12.0f;
    c->layout.padding_right = 12.0f;
    c->layout.gap = 6.0f;
    static const struct yetty_ygui_widget_vtable collapsing_header_vtable = {
        .render = collapsing_header_render,
        .render_all = collapsing_header_render_all,
        .on_press = collapsing_header_on_press,
        .destroy = collapsing_header_destroy,
    };
    c->vtable = &collapsing_header_vtable;
    add_to_engine(engine, c);
    return c;
}

void yetty_ygui_widget_collapsing_header_set_label(struct yetty_ygui_widget *widget,
                                                   const char *label)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COLLAPSING_HEADER) {
        return;
    }
    free(widget->data.collapsing_header.label);
    widget->data.collapsing_header.label = ygui_strdup(label);
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

const char *yetty_ygui_widget_collapsing_header_get_label(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COLLAPSING_HEADER) {
        return NULL;
    }
    return widget->data.collapsing_header.label;
}

void yetty_ygui_widget_collapsing_header_set_open(struct yetty_ygui_widget *widget, int open)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COLLAPSING_HEADER) {
        return;
    }
    int was_open = (widget->flags & YETTY_YGUI_FLAG_OPEN) != 0;
    if (open) {
        widget->flags |= YETTY_YGUI_FLAG_OPEN;
    } else {
        widget->flags &= ~YETTY_YGUI_FLAG_OPEN;
    }
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
        /* OPEN → close: the header keeps rendering, but the children
         * stop. Queue deletes for the children's subtrees only — the
         * header itself will re-emit via emit_self_in_group normally. */
        if (was_open && !open) {
            for (struct yetty_ygui_widget *c = widget->first_child; c; c = c->next_sibling) {
                yetty_ygui_internal_queue_delete_subtree_rendered(c);
            }
        }
    }
}

int yetty_ygui_widget_collapsing_header_is_open(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COLLAPSING_HEADER) {
        return 0;
    }
    return (widget->flags & YETTY_YGUI_FLAG_OPEN) ? 1 : 0;
}

/*=============================================================================
 * Tooltip Widget
 *===========================================================================*/

static struct yetty_ycore_void_result tooltip_render(struct yetty_ygui_widget *self,
                                                     struct yetty_ygui_render_ctx *ctx)
{
    if (!self->data.tooltip.label || !self->data.tooltip.label[0]) {
        return YETTY_OK_VOID();
    }
    const struct yetty_ygui_theme *t = ctx->theme;
    yetty_ygui_render_ctx_render_box_shadow(ctx, self->x, self->y, self->w, self->h,
                                            t->radius_medium, t->elevation_medium, t->shadow,
                                            t->elevation_alpha);
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, t->tooltip_bg,
                                     t->radius_medium);
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h,
                                             t->border_muted, t->radius_medium, 1.0f);
    yetty_ygui_render_ctx_render_text(ctx, self->data.tooltip.label, self->x + t->pad_large - 2,
                                      self->y + t->pad_medium, self->fg_color, t->font_size);
    return YETTY_OK_VOID();
}

static void tooltip_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.tooltip.label);
}

struct yetty_ygui_widget *yetty_ygui_engine_tooltip(struct yetty_ygui_engine *engine,
                                                    const char *id, float x, float y, float w,
                                                    float h, const char *label)
{
    struct yetty_ygui_widget *tt =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_TOOLTIP, id);
    if (!tt) {
        return NULL;
    }
    yetty_ygui_widget_init_base(tt, x, y, w, h);
    tt->data.tooltip.label = ygui_strdup(label);
    static const struct yetty_ygui_widget_vtable tooltip_vtable = {
        .render = tooltip_render,
        .destroy = tooltip_destroy,
    };
    tt->vtable = &tooltip_vtable;
    add_to_engine(engine, tt);
    return tt;
}

void yetty_ygui_widget_tooltip_set_label(struct yetty_ygui_widget *widget, const char *label)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TOOLTIP) {
        return;
    }
    free(widget->data.tooltip.label);
    widget->data.tooltip.label = ygui_strdup(label);
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

const char *yetty_ygui_widget_tooltip_get_label(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TOOLTIP) {
        return NULL;
    }
    return widget->data.tooltip.label;
}

/*=============================================================================
 * Selectable Widget
 *
 * List item that toggles its checked state on press.
 *===========================================================================*/

static struct yetty_ycore_void_result selectable_render(struct yetty_ygui_widget *self,
                                                        struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    if (self->flags & YETTY_YGUI_FLAG_CHECKED) {
        yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h,
                                         self->accent_color, t->radius_small);
    } else if (self->flags & YETTY_YGUI_FLAG_HOVER) {
        yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, t->bg_hover,
                                         t->radius_small);
    }
    if (self->data.selectable.label) {
        yetty_ygui_render_ctx_render_text(ctx, self->data.selectable.label, self->x + t->pad_large,
                                          self->y + t->pad_medium, self->fg_color, t->font_size);
    }
    return YETTY_OK_VOID();
}

static int selectable_on_press(struct yetty_ygui_widget *self, float lx, float ly,
                               ygui_event_t *out)
{
    (void)lx;
    (void)ly;
    self->flags ^= YETTY_YGUI_FLAG_CHECKED;
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CLICK;
    out->data.bool_value = (self->flags & YETTY_YGUI_FLAG_CHECKED) ? 1 : 0;
    return 1;
}

static void selectable_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.selectable.label);
}

struct yetty_ygui_widget *yetty_ygui_engine_selectable(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y, float w,
                                                       float h, const char *label)
{
    struct yetty_ygui_widget *s =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_SELECTABLE, id);
    if (!s) {
        return NULL;
    }
    yetty_ygui_widget_init_base(s, x, y, w, h);
    s->data.selectable.label = ygui_strdup(label);
    static const struct yetty_ygui_widget_vtable selectable_vtable = {
        .render = selectable_render,
        .on_press = selectable_on_press,
        .destroy = selectable_destroy,
    };
    s->vtable = &selectable_vtable;
    add_to_engine(engine, s);
    return s;
}

void yetty_ygui_widget_selectable_set_label(struct yetty_ygui_widget *widget, const char *label)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SELECTABLE) {
        return;
    }
    free(widget->data.selectable.label);
    widget->data.selectable.label = ygui_strdup(label);
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

const char *yetty_ygui_widget_selectable_get_label(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SELECTABLE) {
        return NULL;
    }
    return widget->data.selectable.label;
}

void yetty_ygui_widget_selectable_set_checked(struct yetty_ygui_widget *widget, int checked)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SELECTABLE) {
        return;
    }
    if (checked) {
        widget->flags |= YETTY_YGUI_FLAG_CHECKED;
    } else {
        widget->flags &= ~YETTY_YGUI_FLAG_CHECKED;
    }
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

int yetty_ygui_widget_selectable_is_checked(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SELECTABLE) {
        return 0;
    }
    return (widget->flags & YETTY_YGUI_FLAG_CHECKED) ? 1 : 0;
}

/*=============================================================================
 * ChoiceBox Widget
 *
 * Vertical radio-button list. Press maps localY → option index.
 *===========================================================================*/

static void choicebox_free_options(struct yetty_ygui_widget *self)
{
    if (self->data.choicebox.options) {
        for (int i = 0; i < self->data.choicebox.option_count; i++) {
            free(self->data.choicebox.options[i]);
        }
        free(self->data.choicebox.options);
        self->data.choicebox.options = NULL;
    }
    self->data.choicebox.option_count = 0;
}

static void choicebox_copy_options(struct yetty_ygui_widget *self, const char **options, int count)
{
    choicebox_free_options(self);
    if (!options || count <= 0) {
        return;
    }
    self->data.choicebox.options = (char **)malloc(count * sizeof(char *));
    if (!self->data.choicebox.options) {
        return;
    }
    for (int i = 0; i < count; i++) {
        self->data.choicebox.options[i] = ygui_strdup(options[i]);
    }
    self->data.choicebox.option_count = count;
}

static struct yetty_ycore_void_result choicebox_render(struct yetty_ygui_widget *self,
                                                       struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float opt_h = t->row_height;
    float radio_size = 14.0f;
    float cy = self->y;
    for (int i = 0; i < self->data.choicebox.option_count; i++) {
        int is_selected = (i == self->data.choicebox.selected);
        int is_hovered = (i == self->data.choicebox.hover_index);
        float center_x = self->x + radio_size * 0.5f;
        float center_y = cy + t->pad_medium + radio_size * 0.5f;

        yetty_ygui_render_ctx_render_circle_outline(
            ctx, center_x, center_y, radio_size * 0.5f,
            is_hovered ? self->accent_color : t->border_muted, 1.5f);
        if (is_selected) {
            yetty_ygui_render_ctx_render_circle(ctx, center_x, center_y, radio_size * 0.25f,
                                                self->accent_color);
        } else if (is_hovered) {
            yetty_ygui_render_ctx_render_circle(ctx, center_x, center_y, radio_size / 6.0f,
                                                t->thumb_hover);
        }

        if (self->data.choicebox.options[i]) {
            yetty_ygui_render_ctx_render_text(ctx, self->data.choicebox.options[i],
                                              self->x + radio_size + t->pad_large,
                                              cy + t->pad_medium, self->fg_color, t->font_size);
        }
        cy += opt_h;
    }
    return YETTY_OK_VOID();
}

static int choicebox_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)lx;
    const struct yetty_ygui_theme *t = self->engine->theme;
    float opt_h = t->row_height;
    int idx = (int)(ly / opt_h);
    if (idx < 0 || idx >= self->data.choicebox.option_count) {
        return 0;
    }
    self->data.choicebox.selected = idx;
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.int_value = idx;
    return 1;
}

static void choicebox_destroy(struct yetty_ygui_widget *self)
{
    choicebox_free_options(self);
}

struct yetty_ygui_widget *yetty_ygui_engine_choicebox(struct yetty_ygui_engine *engine,
                                                      const char *id, float x, float y, float w,
                                                      float h, const char **options,
                                                      int option_count)
{
    struct yetty_ygui_widget *c =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_CHOICEBOX, id);
    if (!c) {
        return NULL;
    }
    yetty_ygui_widget_init_base(c, x, y, w, h);
    c->data.choicebox.options = NULL;
    c->data.choicebox.option_count = 0;
    c->data.choicebox.selected = 0;
    c->data.choicebox.hover_index = -1;
    choicebox_copy_options(c, options, option_count);
    static const struct yetty_ygui_widget_vtable choicebox_vtable = {
        .render = choicebox_render,
        .on_press = choicebox_on_press,
        .destroy = choicebox_destroy,
    };
    c->vtable = &choicebox_vtable;
    add_to_engine(engine, c);
    return c;
}

void yetty_ygui_widget_choicebox_set_options(struct yetty_ygui_widget *widget, const char **options,
                                             int count)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_CHOICEBOX) {
        return;
    }
    choicebox_copy_options(widget, options, count);
    if (widget->data.choicebox.selected >= count) {
        widget->data.choicebox.selected = count > 0 ? 0 : -1;
    }
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

void yetty_ygui_widget_choicebox_set_selected(struct yetty_ygui_widget *widget, int index)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_CHOICEBOX) {
        return;
    }
    widget->data.choicebox.selected = index;
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

int yetty_ygui_widget_choicebox_get_selected(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_CHOICEBOX) {
        return 0;
    }
    return widget->data.choicebox.selected;
}

/*=============================================================================
 * Scrollbar Widgets (vertical + horizontal)
 *
 * Standalone scrollbar; drag thumb to set value in [0..1].
 *===========================================================================*/

/* Scrollbar geometry helpers — read from the bound target when one is
 * attached, fall back to the free-running 0..1 value otherwise. */

static float vscrollbar_value(const struct yetty_ygui_widget *self)
{
    struct yetty_ygui_widget *tgt = self->data.scrollbar.target;
    if (tgt && tgt->scrollable) {
        float max_s = tgt->scrollable->get_max_scroll(tgt);
        if (max_s <= 0.0f) {
            return 0.0f;
        }
        return tgt->scrollable->get_scroll(tgt) / max_s;
    }
    return self->data.scrollbar.value;
}

static float vscrollbar_thumb_h(const struct yetty_ygui_widget *self)
{
    /* When bound: thumb size is proportional to viewport / content —
     * the bigger the document the smaller the thumb. Floor at 20 px
     * so it stays grabbable. Free-running mode keeps the legacy 20%
     * appearance. */
    struct yetty_ygui_widget *tgt = self->data.scrollbar.target;
    if (tgt && tgt->scrollable) {
        float content = tgt->scrollable->get_content_h(tgt);
        if (content > 0) {
            float ratio = tgt->scrollable->get_viewport_h(tgt) / content;
            return ygui_max(20.0f, self->h * ygui_clamp(ratio, 0.05f, 1.0f));
        }
    }
    return ygui_max(20.0f, self->h * 0.2f);
}

static struct yetty_ycore_void_result vscrollbar_render(struct yetty_ygui_widget *self,
                                                        struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float track_w = self->w > 0 ? self->w : t->scrollbar_size;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, track_w, self->h, t->bg_secondary,
                                     track_w * 0.5f);

    float thumb_h = vscrollbar_thumb_h(self);
    float track_range = self->h - thumb_h;
    float thumb_y = self->y + vscrollbar_value(self) * track_range;
    uint32_t thumb_color =
        (self->flags & YETTY_YGUI_FLAG_PRESSED)
            ? self->accent_color
            : (self->flags & YETTY_YGUI_FLAG_HOVER ? t->thumb_hover : t->thumb_normal);
    yetty_ygui_render_ctx_render_box(ctx, self->x + t->pad_small, thumb_y, track_w - t->pad_medium,
                                     thumb_h, thumb_color, (track_w - t->pad_medium) * 0.5f);
    return YETTY_OK_VOID();
}

static int vscrollbar_update(struct yetty_ygui_widget *self, float ly, ygui_event_t *out)
{
    float thumb_h = vscrollbar_thumb_h(self);
    float track_range = self->h - thumb_h;
    if (track_range <= 0) {
        return 0;
    }
    float pct = (ly - thumb_h * 0.5f) / track_range;
    pct = ygui_clamp(pct, 0.0f, 1.0f);

    struct yetty_ygui_widget *tgt = self->data.scrollbar.target;
    if (tgt && tgt->scrollable) {
        float max_s = tgt->scrollable->get_max_scroll(tgt);
        tgt->scrollable->scroll_to(tgt, pct * max_s);
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_SCROLL;
        out->data.scroll.x = 0;
        out->data.scroll.y = tgt->scrollable->get_scroll(tgt);
        return 1;
    }

    /* Free-running fallback: own the 0..1 value, fire change_callback. */
    self->data.scrollbar.value = pct;
    if (self->change_callback) {
        self->change_callback(self, pct, self->change_userdata);
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.float_value = pct;
    return 1;
}

static int vscrollbar_on_press(struct yetty_ygui_widget *self, float lx, float ly,
                               ygui_event_t *out)
{
    (void)lx;
    return vscrollbar_update(self, ly, out);
}

static int vscrollbar_on_drag(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)lx;
    return vscrollbar_update(self, ly, out);
}

/* Wheel on the scrollbar — forward to the bound target so the user
 * gets the same scroll-on-wheel UX everywhere along the strip. */
static int vscrollbar_on_scroll(struct yetty_ygui_widget *self, float dx, float dy,
                                ygui_event_t *out)
{
    (void)dx;
    struct yetty_ygui_widget *tgt = self->data.scrollbar.target;
    if (!tgt || !tgt->scrollable) {
        return 0;
    }
    const float speed = 60.0f;
    float prev = tgt->scrollable->get_scroll(tgt);
    tgt->scrollable->scroll_to(tgt, prev - dy * speed);
    float now = tgt->scrollable->get_scroll(tgt);
    if (now == prev) {
        return 0;
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_SCROLL;
    out->data.scroll.x = 0;
    out->data.scroll.y = now;
    return 1;
}

struct yetty_ygui_widget *yetty_ygui_engine_vscrollbar(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y, float w,
                                                       float h)
{
    struct yetty_ygui_widget *sb =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_VSCROLLBAR, id);
    if (!sb) {
        return NULL;
    }
    yetty_ygui_widget_init_base(sb, x, y, w, h);
    sb->data.scrollbar.value = 0;
    sb->data.scrollbar.target = NULL;
    static const struct yetty_ygui_widget_vtable vscrollbar_vtable = {
        .render = vscrollbar_render,
        .on_press = vscrollbar_on_press,
        .on_drag = vscrollbar_on_drag,
        .on_scroll = vscrollbar_on_scroll,
    };
    sb->vtable = &vscrollbar_vtable;
    add_to_engine(engine, sb);
    return sb;
}

static struct yetty_ycore_void_result hscrollbar_render(struct yetty_ygui_widget *self,
                                                        struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float track_h = self->h > 0 ? self->h : t->scrollbar_size;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, track_h, t->bg_secondary,
                                     track_h * 0.5f);

    float thumb_w = ygui_max(20.0f, self->w * 0.2f);
    float track_range = self->w - thumb_w;
    float thumb_x = self->x + self->data.scrollbar.value * track_range;
    uint32_t thumb_color =
        (self->flags & YETTY_YGUI_FLAG_PRESSED)
            ? self->accent_color
            : (self->flags & YETTY_YGUI_FLAG_HOVER ? t->thumb_hover : t->thumb_normal);
    yetty_ygui_render_ctx_render_box(ctx, thumb_x, self->y + t->pad_small, thumb_w,
                                     track_h - t->pad_medium, thumb_color,
                                     (track_h - t->pad_medium) * 0.5f);
    return YETTY_OK_VOID();
}

static int hscrollbar_update(struct yetty_ygui_widget *self, float lx, ygui_event_t *out)
{
    float thumb_w = ygui_max(20.0f, self->w * 0.2f);
    float track_range = self->w - thumb_w;
    if (track_range <= 0) {
        return 0;
    }
    float pct = (lx - thumb_w * 0.5f) / track_range;
    self->data.scrollbar.value = ygui_clamp(pct, 0.0f, 1.0f);
    if (self->change_callback) {
        self->change_callback(self, self->data.scrollbar.value, self->change_userdata);
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.float_value = self->data.scrollbar.value;
    return 1;
}

static int hscrollbar_on_press(struct yetty_ygui_widget *self, float lx, float ly,
                               ygui_event_t *out)
{
    (void)ly;
    return hscrollbar_update(self, lx, out);
}

static int hscrollbar_on_drag(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)ly;
    return hscrollbar_update(self, lx, out);
}

struct yetty_ygui_widget *yetty_ygui_engine_hscrollbar(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y, float w,
                                                       float h)
{
    struct yetty_ygui_widget *sb =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_HSCROLLBAR, id);
    if (!sb) {
        return NULL;
    }
    yetty_ygui_widget_init_base(sb, x, y, w, h);
    sb->data.scrollbar.value = 0;
    static const struct yetty_ygui_widget_vtable hscrollbar_vtable = {
        .render = hscrollbar_render,
        .on_press = hscrollbar_on_press,
        .on_drag = hscrollbar_on_drag,
    };
    sb->vtable = &hscrollbar_vtable;
    add_to_engine(engine, sb);
    return sb;
}

void yetty_ygui_widget_scrollbar_set_value(struct yetty_ygui_widget *widget, float value)
{
    if (!widget) {
        return;
    }
    if (widget->type != YETTY_YGUI_WIDGET_VSCROLLBAR &&
        widget->type != YETTY_YGUI_WIDGET_HSCROLLBAR) {
        return;
    }
    widget->data.scrollbar.value = ygui_clamp(value, 0.0f, 1.0f);
    if (widget->engine) {
        widget->engine->dirty = 1; widget->dirty = 1;
    }
}

void yetty_ygui_widget_scrollbar_bind(struct yetty_ygui_widget *scrollbar,
                                      struct yetty_ygui_widget *target)
{
    if (!scrollbar) {
        return;
    }
    if (scrollbar->type != YETTY_YGUI_WIDGET_VSCROLLBAR &&
        scrollbar->type != YETTY_YGUI_WIDGET_HSCROLLBAR) {
        return;
    }
    /* Detach any previously-bound target from this scrollbar's observer
     * slot — keeps target->scroll_observer pointing only at the latest
     * bound bar (and only if no other view has claimed the slot since). */
    struct yetty_ygui_widget *prev = scrollbar->data.scrollbar.target;
    if (prev && prev->scroll_observer == scrollbar) {
        prev->scroll_observer = NULL;
    }
    scrollbar->data.scrollbar.target = target;
    if (target) {
        target->scroll_observer = scrollbar;
    }
    if (scrollbar->engine) {
        scrollbar->engine->dirty = 1;
        scrollbar->dirty = 1;
    }
}

float yetty_ygui_widget_scrollbar_get_value(const struct yetty_ygui_widget *widget)
{
    if (!widget) {
        return 0;
    }
    if (widget->type != YETTY_YGUI_WIDGET_VSCROLLBAR &&
        widget->type != YETTY_YGUI_WIDGET_HSCROLLBAR) {
        return 0;
    }
    return widget->data.scrollbar.value;
}

/*=============================================================================
 * List Widget — generic row-aware vertical container with selection.
 *===========================================================================*/

/* Inner: emit only the list's selection-background drawable. Called via
 * widget_emit_self_in_group, so it lands inside the list's GROUP. */
static struct yetty_ycore_void_result list_render(struct yetty_ygui_widget *self,
                                                  struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    struct yetty_ygui_widget *sel = self->data.list.selected;
    if (sel && (sel->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        return yetty_ygui_render_ctx_render_box(ctx, sel->x, sel->y, sel->w, sel->h,
                                                t->selection_bg, t->radius_small);
    }
    return YETTY_OK_VOID();
}

/* Custom render_all so the selection background lands at the correct
 * absolute position. The default render_all_default calls render() while
 * ctx->offset still points at the *parent's* origin — wrong frame for
 * drawing a box at a *child's* relative coords. We instead push offset
 * to self->layout_x/y first, paint the selection rect, then recurse
 * normally. The list draws nothing else decorative; children render
 * their own surfaces. */
static struct yetty_ycore_void_result list_render_all(struct yetty_ygui_widget *self,
                                                      struct yetty_ygui_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        return YETTY_OK_VOID();
    }
    self->was_rendered = 1;

    /* Open list's CMD_GROUP (parent-relative rect), emit selection
     * background body, then recurse children INSIDE the open scope so
     * they nest. Children's positions are already parent-relative. */
    uint32_t marker = yetty_ygui_widget_open_group(self, ctx, list_render);

    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    for (struct yetty_ygui_widget *child = self->first_child; child; child = child->next_sibling) {
        if (!(child->flags & YETTY_YGUI_FLAG_VISIBLE)) {
            continue;
        }
        struct yetty_ycore_void_result r;
        if (child->vtable && child->vtable->render_all) {
            r = child->vtable->render_all(child, ctx);
        } else {
            r = yetty_ygui_widget_render_all_default(child, ctx);
        }
        if (YETTY_IS_ERR(r) && YETTY_IS_OK(first_err)) {
            first_err = r;
        } else if (YETTY_IS_ERR(r)) {
            yetty_ycore_error_destroy(r.error);
        }
    }

    yetty_ygui_widget_close_group(self, ctx, marker);
    return first_err;
}

/* Find the nearest child that contains (lx, ly) in this widget's local
 * coordinate space. Returns NULL if no hit. */
static struct yetty_ygui_widget *list_child_at(struct yetty_ygui_widget *self, float lx, float ly)
{
    /* Children's x/y are relative to parent (self). */
    for (struct yetty_ygui_widget *c = self->first_child; c; c = c->next_sibling) {
        if (!(c->flags & YETTY_YGUI_FLAG_VISIBLE)) {
            continue;
        }
        if (lx >= c->x && lx < c->x + c->w && ly >= c->y && ly < c->y + c->h) {
            return c;
        }
    }
    return NULL;
}

static int list_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    struct yetty_ygui_widget *child = list_child_at(self, lx, ly);
    if (!child) {
        return 0;
    }
    if (self->data.list.selected != child) {
        self->data.list.selected = child;
        if (self->engine) {
            self->engine->dirty = 1; self->dirty = 1;
        }
    }
    if (self->data.list.on_select) {
        self->data.list.on_select(child, self->data.list.on_select_userdata);
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    return 1;
}

struct yetty_ygui_widget *yetty_ygui_engine_list(struct yetty_ygui_engine *engine, const char *id,
                                                 float x, float y, float w, float h)
{
    struct yetty_ygui_widget *lst =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_LIST, id);
    if (!lst) {
        return NULL;
    }
    yetty_ygui_widget_init_base(lst, x, y, w, h);
    /* Default layout: flex column with theme gap; children stretched on
     * the cross axis so each row spans the list width. */
    lst->layout.mode = YETTY_YGUI_LAYOUT_FLEX;
    lst->layout.direction = YETTY_YGUI_FLEX_COLUMN;
    lst->layout.align_items = YETTY_YGUI_ALIGN_STRETCH;
    lst->layout.gap = engine->theme->pad_small;
    static const struct yetty_ygui_widget_vtable list_vtable = {
        .render_all = list_render_all,
        .on_press = list_on_press,
    };
    lst->vtable = &list_vtable;
    add_to_engine(engine, lst);
    return lst;
}

void yetty_ygui_widget_list_set_selected(struct yetty_ygui_widget *list,
                                         struct yetty_ygui_widget *child)
{
    if (!list || list->type != YETTY_YGUI_WIDGET_LIST) {
        return;
    }
    list->data.list.selected = child;
    if (list->engine) {
        list->engine->dirty = 1; list->dirty = 1;
    }
}

struct yetty_ygui_widget *yetty_ygui_widget_list_get_selected(const struct yetty_ygui_widget *list)
{
    if (!list || list->type != YETTY_YGUI_WIDGET_LIST) {
        return NULL;
    }
    return list->data.list.selected;
}

void yetty_ygui_widget_list_on_select(struct yetty_ygui_widget *list, ygui_click_callback_t cb,
                                      void *userdata)
{
    if (!list || list->type != YETTY_YGUI_WIDGET_LIST) {
        return;
    }
    list->data.list.on_select = cb;
    list->data.list.on_select_userdata = userdata;
}

/*=============================================================================
 * Table Widget — header row + N×M cell grid of plain strings.
 *
 * Self-rendered (no child widgets). Cells own their string copies and are
 * freed on destroy / clear_rows. Click on a data row fires on_select(row).
 *===========================================================================*/

static char *strdup_or_null(const char *s)
{
    if (!s) {
        return NULL;
    }
    size_t n = strlen(s);
    char *r = malloc(n + 1);
    if (!r) {
        return NULL;
    }
    memcpy(r, s, n + 1);
    return r;
}

static void table_free_row(char **row, int n_cells)
{
    if (!row) {
        return;
    }
    for (int c = 0; c < n_cells; c++) {
        free(row[c]);
    }
    free(row);
}

static char **table_dup_row(const char *const *cells, int n_cells)
{
    char **row = calloc((size_t)n_cells, sizeof(char *));
    if (!row) {
        return NULL;
    }
    for (int c = 0; c < n_cells; c++) {
        row[c] = strdup_or_null(cells[c] ? cells[c] : "");
    }
    return row;
}

/* Resolved per-column widths in pixels. Stretch columns share the leftover
 * space evenly. Caller provides an out array sized n_columns. */
static void table_resolve_widths(const struct yetty_ygui_widget *self, float *out)
{
    int n = self->data.table.n_columns;
    if (n <= 0) {
        return;
    }
    float total_fixed = 0.0f;
    int stretch_count = 0;
    for (int c = 0; c < n; c++) {
        float w = self->data.table.column_widths[c];
        if (w > 0.0f) {
            total_fixed += w;
        } else {
            stretch_count++;
        }
    }
    float stretch_w = 0.0f;
    if (stretch_count > 0) {
        float leftover = self->w - total_fixed;
        if (leftover < 0.0f) {
            leftover = 0.0f;
        }
        stretch_w = leftover / (float)stretch_count;
    }
    for (int c = 0; c < n; c++) {
        float w = self->data.table.column_widths[c];
        out[c] = (w > 0.0f) ? w : stretch_w;
    }
}

static struct yetty_ycore_void_result table_render(struct yetty_ygui_widget *self,
                                                   struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    int n = self->data.table.n_columns;
    if (n <= 0) {
        /* No columns set yet — just paint the surface and bail. */
        yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, t->bg_surface,
                                         t->radius_small);
        return YETTY_OK_VOID();
    }
    float row_h = self->data.table.row_height > 0.0f ? self->data.table.row_height : t->row_height;
    if (row_h <= 0.0f) {
        row_h = 24.0f;
    }
    float font_size = t->font_size > 0.0f ? t->font_size : 12.0f;
    float text_pad_x = 6.0f;
    float text_y_off = (row_h - font_size) * 0.5f;

    float widths[64]; /* practical cap; heroic tables can grow this */
    if (n > (int)(sizeof(widths) / sizeof(widths[0]))) {
        n = (int)(sizeof(widths) / sizeof(widths[0]));
    }
    table_resolve_widths(self, widths);

    /* Surface. */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, t->bg_surface,
                                     t->radius_small);

    /* Header row. */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, row_h, t->bg_header,
                                     t->radius_small);
    {
        float cx = self->x;
        for (int c = 0; c < n; c++) {
            const char *name =
                self->data.table.column_names[c] ? self->data.table.column_names[c] : "";
            yetty_ygui_render_ctx_render_text(ctx, name, cx + text_pad_x, self->y + text_y_off,
                                              t->text_primary, font_size);
            cx += widths[c];
            /* Vertical separator after every column except the last. */
            if (c < n - 1) {
                yetty_ygui_render_ctx_render_box(ctx, cx, self->y, 1.0f, self->h, t->border_muted,
                                                 0.0f);
            }
        }
    }
    /* Header / data divider. */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y + row_h, self->w, 1.0f, t->border, 0.0f);

    /* Data rows. */
    int rows = self->data.table.n_rows;
    int sel = self->data.table.selected_row;
    for (int r = 0; r < rows; r++) {
        float row_y = self->y + row_h + (float)r * row_h;
        if (row_y + row_h > self->y + self->h) {
            /* Clip — table is too short for this row. Stop drawing. */
            break;
        }
        if (r == sel) {
            yetty_ygui_render_ctx_render_box(ctx, self->x, row_y, self->w, row_h, t->selection_bg,
                                             0.0f);
        } else if ((r & 1) == 1 && t->bg_secondary != 0u) {
            /* Zebra striping for odd-indexed rows when the theme provides
             * a distinct secondary surface. */
            yetty_ygui_render_ctx_render_box(ctx, self->x, row_y, self->w, row_h, t->bg_secondary,
                                             0.0f);
        }
        float cx = self->x;
        char **row = self->data.table.rows[r];
        for (int c = 0; c < n; c++) {
            const char *txt = row ? row[c] : NULL;
            if (txt) {
                yetty_ygui_render_ctx_render_text(ctx, txt, cx + text_pad_x, row_y + text_y_off,
                                                  t->text_primary, font_size);
            }
            cx += widths[c];
        }
    }

    /* Outer border last so it sits above the rows. */
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h, t->border,
                                             t->radius_small, 1.0f);
    return YETTY_OK_VOID();
}

/* Cycle the table's sort state for the given column. Reorders rows
 * (and keeps `selected_row` pinned to its original pointer's new
 * index). */
static int table_str_cmp_asc(const void *a, const void *b)
{
    const char *sa = *(const char *const *)a ? *(const char *const *)a : "";
    const char *sb = *(const char *const *)b ? *(const char *const *)b : "";
    return strcmp(sa, sb);
}

static void table_apply_sort(struct yetty_ygui_widget *self)
{
    int col = self->data.table.sort_column;
    int n = self->data.table.n_rows;
    int nc = self->data.table.n_columns;
    if (col < 0 || col >= nc || n <= 1) {
        return;
    }
    /* Indirect sort: build an array of row pointers + their column
     * values, qsort that, then write the rows array back. Preserves
     * row pointer identity so selection survives. */
    char ***rows = self->data.table.rows;
    char **prev_sel_ptr = (self->data.table.selected_row >= 0 &&
                           self->data.table.selected_row < n)
                              ? rows[self->data.table.selected_row]
                              : NULL;
    struct pair { char *key; char **row; } *pairs = malloc((size_t)n * sizeof(*pairs));
    if (!pairs) return;
    for (int i = 0; i < n; i++) {
        pairs[i].key = (rows[i] && col < nc) ? rows[i][col] : NULL;
        pairs[i].row = rows[i];
    }
    qsort(pairs, (size_t)n, sizeof(*pairs), table_str_cmp_asc);
    if (self->data.table.sort_order == 1) {
        /* descending — reverse */
        for (int i = 0, j = n - 1; i < j; i++, j--) {
            struct pair tmp = pairs[i];
            pairs[i] = pairs[j];
            pairs[j] = tmp;
        }
    }
    int new_sel = -1;
    for (int i = 0; i < n; i++) {
        rows[i] = pairs[i].row;
        if (rows[i] == prev_sel_ptr) new_sel = i;
    }
    free(pairs);
    self->data.table.selected_row = new_sel;
}

static void table_header_clicked(struct yetty_ygui_widget *self, int col)
{
    if (col < 0 || col >= self->data.table.n_columns) return;
    if (self->data.table.sort_column != col) {
        self->data.table.sort_column = col;
        self->data.table.sort_order = 0; /* asc */
    } else if (self->data.table.sort_order == 0) {
        self->data.table.sort_order = 1; /* desc */
    } else {
        self->data.table.sort_column = -1;
        self->data.table.sort_order = 0;
    }
    table_apply_sort(self);
    self->dirty = 1;
    if (self->engine) self->engine->dirty = 1;
}

#define TABLE_RESIZE_GRIP_W 6.0f

static int table_on_drag(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)ly;
    if (self->data.table.resizing_column < 0) {
        return 0;
    }
    int col = self->data.table.resizing_column;
    float delta = lx - self->data.table.resize_start_x;
    float new_w = self->data.table.resize_start_w + delta;
    if (new_w < 20.0f) new_w = 20.0f;
    self->data.table.column_widths[col] = new_w;
    self->dirty = 1;
    if (self->engine) self->engine->dirty = 1;
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    return 1;
}

static int table_on_release(struct yetty_ygui_widget *self, float lx, float ly,
                            ygui_event_t *out)
{
    (void)lx; (void)ly; (void)out;
    self->data.table.resizing_column = -1;
    return 0;
}

static int table_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    const struct yetty_ygui_theme *t = self->engine ? self->engine->theme : NULL;
    if (!t) {
        return 0;
    }
    float row_h = self->data.table.row_height > 0.0f ? self->data.table.row_height : t->row_height;
    if (row_h <= 0.0f) {
        row_h = 24.0f;
    }
    /* Header strip — first dispatch resize-grip hit, then sort click. */
    if (ly < row_h) {
        float widths[64];
        int nc = self->data.table.n_columns;
        if (nc > (int)(sizeof(widths) / sizeof(widths[0]))) {
            nc = (int)(sizeof(widths) / sizeof(widths[0]));
        }
        table_resolve_widths(self, widths);
        float cx = 0.0f;
        for (int c = 0; c < nc; c++) {
            float col_right = cx + widths[c];
            /* Resize grip on the right edge — skip the last column
             * (it stretches; no fixed-width semantics for it). */
            if (c < nc - 1 && lx >= col_right - TABLE_RESIZE_GRIP_W && lx < col_right) {
                self->data.table.resizing_column = c;
                self->data.table.resize_start_w =
                    self->data.table.column_widths[c] > 0 ? self->data.table.column_widths[c]
                                                          : widths[c];
                self->data.table.resize_start_x = lx;
                return 0;
            }
            if (lx >= cx && lx < col_right) {
                table_header_clicked(self, c);
                out->widget_id = self->id;
                out->type = YETTY_YGUI_EVENT_CHANGE;
                out->data.int_value = self->data.table.sort_column;
                return 1;
            }
            cx = col_right;
        }
        return 0;
    }
    /* Body — original selection path. */
    int row = (int)((ly - row_h) / row_h);
    if (row < 0 || row >= self->data.table.n_rows) {
        return 0;
    }
    self->data.table.selected_row = row;
    if (self->data.table.on_select) {
        self->data.table.on_select(self, row, self->data.table.on_select_userdata);
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.int_value = row;
    return 1;
}

static void table_destroy(struct yetty_ygui_widget *self)
{
    int n_cols = self->data.table.n_columns;
    for (int r = 0; r < self->data.table.n_rows; r++) {
        table_free_row(self->data.table.rows[r], n_cols);
    }
    free(self->data.table.rows);
    for (int c = 0; c < n_cols; c++) {
        free(self->data.table.column_names[c]);
    }
    free(self->data.table.column_names);
    free(self->data.table.column_widths);
    self->data.table.rows = NULL;
    self->data.table.column_names = NULL;
    self->data.table.column_widths = NULL;
    self->data.table.n_columns = 0;
    self->data.table.n_rows = 0;
    self->data.table.row_capacity = 0;
}

struct yetty_ygui_widget *yetty_ygui_engine_table(struct yetty_ygui_engine *engine, const char *id,
                                                  float x, float y, float w, float h)
{
    struct yetty_ygui_widget *tbl =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_TABLE, id);
    if (!tbl) {
        return NULL;
    }
    yetty_ygui_widget_init_base(tbl, x, y, w, h);
    tbl->data.table.selected_row = -1;
    tbl->data.table.sort_column = -1;
    tbl->data.table.sort_order = 0;
    tbl->data.table.resizing_column = -1;
    static const struct yetty_ygui_widget_vtable table_vtable = {
        .render = table_render,
        .on_press = table_on_press,
        .on_drag = table_on_drag,
        .on_release = table_on_release,
        .destroy = table_destroy,
    };
    tbl->vtable = &table_vtable;
    add_to_engine(engine, tbl);
    return tbl;
}

void yetty_ygui_widget_table_set_columns(struct yetty_ygui_widget *table, const char *const *names,
                                         const float *widths, int n_columns)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE || n_columns <= 0) {
        return;
    }
    /* Wipe existing column metadata + any rows (cell counts must match). */
    int old_cols = table->data.table.n_columns;
    for (int r = 0; r < table->data.table.n_rows; r++) {
        table_free_row(table->data.table.rows[r], old_cols);
    }
    free(table->data.table.rows);
    table->data.table.rows = NULL;
    table->data.table.n_rows = 0;
    table->data.table.row_capacity = 0;
    for (int c = 0; c < old_cols; c++) {
        free(table->data.table.column_names[c]);
    }
    free(table->data.table.column_names);
    free(table->data.table.column_widths);

    table->data.table.column_names = calloc((size_t)n_columns, sizeof(char *));
    table->data.table.column_widths = calloc((size_t)n_columns, sizeof(float));
    if (!table->data.table.column_names || !table->data.table.column_widths) {
        free(table->data.table.column_names);
        free(table->data.table.column_widths);
        table->data.table.column_names = NULL;
        table->data.table.column_widths = NULL;
        table->data.table.n_columns = 0;
        return;
    }
    for (int c = 0; c < n_columns; c++) {
        table->data.table.column_names[c] = strdup_or_null(names && names[c] ? names[c] : "");
        table->data.table.column_widths[c] = widths ? widths[c] : 0.0f;
    }
    table->data.table.n_columns = n_columns;
    if (table->engine) {
        table->engine->dirty = 1; table->dirty = 1;
    }
}

void yetty_ygui_widget_table_clear_rows(struct yetty_ygui_widget *table)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return;
    }
    for (int r = 0; r < table->data.table.n_rows; r++) {
        table_free_row(table->data.table.rows[r], table->data.table.n_columns);
        table->data.table.rows[r] = NULL;
    }
    table->data.table.n_rows = 0;
    table->data.table.selected_row = -1;
    if (table->engine) {
        table->engine->dirty = 1; table->dirty = 1;
    }
}

void yetty_ygui_widget_table_add_row(struct yetty_ygui_widget *table, const char *const *cells,
                                     int n_cells)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return;
    }
    if (n_cells != table->data.table.n_columns) {
        return; /* row arity must match column count */
    }
    /* Grow the rows array if needed. */
    if (table->data.table.n_rows >= table->data.table.row_capacity) {
        int new_cap = table->data.table.row_capacity > 0 ? table->data.table.row_capacity * 2 : 8;
        char ***bigger = realloc(table->data.table.rows, (size_t)new_cap * sizeof(char **));
        if (!bigger) {
            return;
        }
        for (int i = table->data.table.row_capacity; i < new_cap; i++) {
            bigger[i] = NULL;
        }
        table->data.table.rows = bigger;
        table->data.table.row_capacity = new_cap;
    }
    char **row = table_dup_row(cells, n_cells);
    if (!row) {
        return;
    }
    table->data.table.rows[table->data.table.n_rows++] = row;
    if (table->engine) {
        table->engine->dirty = 1; table->dirty = 1;
    }
}

void yetty_ygui_widget_table_set_row(struct yetty_ygui_widget *table, int row,
                                     const char *const *cells, int n_cells)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return;
    }
    if (row < 0 || row >= table->data.table.n_rows) {
        return;
    }
    if (n_cells != table->data.table.n_columns) {
        return;
    }
    char **new_row = table_dup_row(cells, n_cells);
    if (!new_row) {
        return;
    }
    table_free_row(table->data.table.rows[row], n_cells);
    table->data.table.rows[row] = new_row;
    if (table->engine) {
        table->engine->dirty = 1; table->dirty = 1;
    }
}

int yetty_ygui_widget_table_row_count(const struct yetty_ygui_widget *table)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return 0;
    }
    return table->data.table.n_rows;
}

void yetty_ygui_widget_table_set_selected(struct yetty_ygui_widget *table, int row)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return;
    }
    if (row < -1 || row >= table->data.table.n_rows) {
        return;
    }
    table->data.table.selected_row = row;
    if (table->engine) {
        table->engine->dirty = 1; table->dirty = 1;
    }
}

int yetty_ygui_widget_table_get_selected(const struct yetty_ygui_widget *table)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return -1;
    }
    return table->data.table.selected_row;
}

void yetty_ygui_widget_table_on_select(struct yetty_ygui_widget *table,
                                       yetty_ygui_table_select_fn cb, void *userdata)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return;
    }
    table->data.table.on_select = cb;
    table->data.table.on_select_userdata = userdata;
}

void yetty_ygui_widget_table_set_row_height(struct yetty_ygui_widget *table, float h)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) {
        return;
    }
    table->data.table.row_height = h;
    if (table->engine) {
        table->engine->dirty = 1; table->dirty = 1;
    }
}

void yetty_ygui_widget_table_set_sortable(struct yetty_ygui_widget *table, int enabled)
{
    /* Sort always works at the data level once `sort_column` is set;
     * this toggle is a hint for future visual affordance (sort arrow
     * in header). Today it just leaves sort_column at -1 when
     * disabled — clicks on headers still trigger sort. */
    (void)enabled;
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) return;
    if (!enabled) {
        table->data.table.sort_column = -1;
        table_apply_sort(table);
    }
}

int yetty_ygui_widget_table_get_sort_column(const struct yetty_ygui_widget *table)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) return -1;
    return table->data.table.sort_column;
}

int yetty_ygui_widget_table_get_sort_order(const struct yetty_ygui_widget *table)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) return 0;
    return table->data.table.sort_order;
}

void yetty_ygui_widget_table_sort_by(struct yetty_ygui_widget *table, int column, int descending)
{
    if (!table || table->type != YETTY_YGUI_WIDGET_TABLE) return;
    table->data.table.sort_column = column;
    table->data.table.sort_order = descending ? 1 : 0;
    table_apply_sort(table);
    if (table->engine) { table->engine->dirty = 1; table->dirty = 1; }
}

/*=============================================================================
 * Tree Node Widget — chevron + label header + auto-allocated children list.
 *===========================================================================*/

#define TREE_CHEVRON_W 16.0f
#define TREE_CHEVRON_PAD 4.0f
#define TREE_INDENT_DEFAULT 20.0f

/* Header height scales with the theme's row_height. The pre-flight in
 * ygui_layout.c also queries this, so it must work without a render
 * context — caller supplies the theme. */
static float tree_node_header_h(const struct yetty_ygui_widget *self,
                                const struct yetty_ygui_theme *theme)
{
    (void)self;
    return theme ? theme->row_height : 24.0f;
}

static struct yetty_ycore_void_result tree_node_render(struct yetty_ygui_widget *self,
                                                       struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    int expanded = self->data.tree_node.expanded;
    int hovered = (self->flags & YETTY_YGUI_FLAG_HOVER) != 0;
    int pressed = (self->flags & YETTY_YGUI_FLAG_PRESSED) != 0;

    float header_h = tree_node_header_h(self, t);

    if (hovered || pressed) {
        uint32_t bg = pressed ? t->bg_header : t->bg_hover;
        yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, header_h, bg,
                                         t->radius_small);
    }

    /* Chevron — always rendered. tree_node represents a folder; whether
     * it currently has children loaded is irrelevant (lazy loading is
     * common). The triangle scales gently with header height. */
    float cx = self->x + TREE_CHEVRON_PAD;
    float cy = self->y + header_h * 0.5f;
    float r = header_h * 0.18f;
    if (r < 4.0f) {
        r = 4.0f;
    }
    if (expanded) {
        yetty_ygui_render_ctx_render_triangle(ctx, cx, cy - r * 0.6f, cx + r * 2.0f, cy - r * 0.6f,
                                              cx + r, cy + r * 0.8f, t->text_primary);
    } else {
        yetty_ygui_render_ctx_render_triangle(ctx, cx, cy - r, cx, cy + r, cx + r * 1.2f, cy,
                                              t->text_primary);
    }

    /* Label */
    if (self->data.tree_node.label) {
        float label_x = self->x + TREE_CHEVRON_W + TREE_CHEVRON_PAD;
        float label_y = self->y + (header_h - t->font_size) * 0.5f;
        yetty_ygui_render_ctx_render_text(ctx, self->data.tree_node.label, label_x, label_y,
                                          self->fg_color, t->font_size);
    }
    return YETTY_OK_VOID();
}

/* tree_node has two layout sections: a fixed-height header row plus the
 * children list. Easiest way to express that with our flex engine is to
 * make tree_node itself a flex column where:
 *   - the header is "implicit" (rendered by tree_node_render at y=0 with
 *     a fixed height TREE_HEADER_H_DEFAULT — the layout pass sees the
 *     header as the widget's own first content_h slot)
 *   - the children list lives at y = header_h
 *
 * To get the children list to sit below the header, we lay it out
 * manually here in a custom render_all (similar to panel_render_all).
 * The children list is the only child widget; we render the header
 * background/chevron/label first, then recurse into the list. */
static struct yetty_ycore_void_result tree_node_render_all(struct yetty_ygui_widget *self,
                                                           struct yetty_ygui_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        return YETTY_OK_VOID();
    }
    self->was_rendered = 1;

    /* Open this node's GROUP, emit the header (chevron + label) body,
     * then recurse the children-list INSIDE the open scope when
     * expanded. kids->x/y are already parent-relative to this node. */
    uint32_t marker = yetty_ygui_widget_open_group(self, ctx, tree_node_render);

    struct yetty_ygui_widget *kids = self->data.tree_node.children_list;
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    if (kids && self->data.tree_node.expanded &&
        (kids->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        if (kids->vtable && kids->vtable->render_all) {
            first_err = kids->vtable->render_all(kids, ctx);
        } else {
            first_err = yetty_ygui_widget_render_all_default(kids, ctx);
        }
    }

    yetty_ygui_widget_close_group(self, ctx, marker);
    return first_err;
}

static int tree_node_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    const struct yetty_ygui_theme *theme = self->engine ? self->engine->theme : NULL;
    float header_h = tree_node_header_h(self, theme);
    int on_chevron = (lx <= TREE_CHEVRON_W + TREE_CHEVRON_PAD) && (ly <= header_h);
    int on_header = (ly <= header_h);

    /* tree_node represents a folder. The chevron always toggles, even
     * if children haven't been loaded yet (lazy expansion: on_toggle
     * fires and the user populates inside the callback). */
    if (on_chevron) {
        self->data.tree_node.expanded = !self->data.tree_node.expanded;
        if (self->data.tree_node.children_list) {
            yetty_ygui_widget_set_visible(self->data.tree_node.children_list,
                                          self->data.tree_node.expanded);
        }
        if (self->data.tree_node.on_toggle) {
            self->data.tree_node.on_toggle(self, self->data.tree_node.expanded,
                                           self->data.tree_node.on_toggle_userdata);
        }
        if (self->engine) {
            self->engine->dirty = 1; self->dirty = 1;
        }
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_CHANGE;
        return 1;
    }
    if (on_header) {
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_PRESS;
        return 1;
    }
    return 0;
}

static void tree_node_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.tree_node.label);
    /* children_list is in the regular child widget hierarchy; the
     * engine's recursive destroy handles it. Nothing to do here. */
}

struct yetty_ygui_widget *yetty_ygui_engine_tree_node(struct yetty_ygui_engine *engine,
                                                      const char *id, const char *label)
{
    struct yetty_ygui_widget *node =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_TREE_NODE, id);
    if (!node) {
        return NULL;
    }
    /* Authored size: full width is filled by parent flex (align: stretch).
     * Height: just the header at construction; the pre-flight in
     * ygui_layout.c grows authored_h on every layout pass to fit the
     * currently-visible children. We use FLEX/COLUMN so the layout
     * places the children list directly below the header. */
    float header_h = engine && engine->theme ? engine->theme->row_height : 24.0f;
    yetty_ygui_widget_init_base(node, 0, 0, 200.0f, header_h);
    node->data.tree_node.label = ygui_strdup(label);
    node->data.tree_node.expanded = 0;
    node->data.tree_node.children_list = NULL;

    /* Layout: flex column. The header occupies the top `header_h`
     * pixels (rendered by tree_node_render at y=0). The children list
     * lives in the content box thanks to padding_top = header_h. */
    node->layout.mode = YETTY_YGUI_LAYOUT_FLEX;
    node->layout.direction = YETTY_YGUI_FLEX_COLUMN;
    node->layout.align_items = YETTY_YGUI_ALIGN_STRETCH;
    node->layout.padding_top = header_h;

    static const struct yetty_ygui_widget_vtable tree_node_vtable = {
        .render = tree_node_render,
        .render_all = tree_node_render_all,
        .on_press = tree_node_on_press,
        .destroy = tree_node_destroy,
    };
    node->vtable = &tree_node_vtable;

    /* Auto-allocate the children list. It's added as a normal child
     * widget — the layout pass places it inside the content box (below
     * the header thanks to padding_top), and it's hidden by default
     * (expanded = 0). */
    char child_id[256];
    snprintf(child_id, sizeof(child_id), "%s.children", id ? id : "tree_node");
    struct yetty_ygui_widget *kids = yetty_ygui_engine_list(engine, child_id, 0, 0, 0, 0);
    if (kids) {
        /* Indent: CSS padding-left on the children list. Users can
         * override with apply_css. */
        kids->layout.padding_left = TREE_INDENT_DEFAULT;
        /* Grow to fill whatever the layout pre-flight reserves for us
         * inside the tree_node's content box (everything below the
         * padding_top header strip). */
        kids->layout.flex_grow = 1.0f;
        yetty_ygui_widget_set_visible(kids, 0);
        yetty_ygui_widget_add_child(node, kids);
        node->data.tree_node.children_list = kids;
    }

    add_to_engine(engine, node);
    return node;
}

void yetty_ygui_widget_tree_node_set_label(struct yetty_ygui_widget *node, const char *label)
{
    if (!node || node->type != YETTY_YGUI_WIDGET_TREE_NODE) {
        return;
    }
    free(node->data.tree_node.label);
    node->data.tree_node.label = ygui_strdup(label);
    if (node->engine) {
        node->engine->dirty = 1; node->dirty = 1;
    }
}

const char *yetty_ygui_widget_tree_node_get_label(const struct yetty_ygui_widget *node)
{
    if (!node || node->type != YETTY_YGUI_WIDGET_TREE_NODE) {
        return NULL;
    }
    return node->data.tree_node.label;
}

void yetty_ygui_widget_tree_node_set_expanded(struct yetty_ygui_widget *node, int expanded)
{
    if (!node || node->type != YETTY_YGUI_WIDGET_TREE_NODE) {
        return;
    }
    node->data.tree_node.expanded = expanded ? 1 : 0;
    if (node->data.tree_node.children_list) {
        yetty_ygui_widget_set_visible(node->data.tree_node.children_list, expanded);
    }
    if (node->engine) {
        node->engine->dirty = 1; node->dirty = 1;
    }
}

int yetty_ygui_widget_tree_node_is_expanded(const struct yetty_ygui_widget *node)
{
    if (!node || node->type != YETTY_YGUI_WIDGET_TREE_NODE) {
        return 0;
    }
    return node->data.tree_node.expanded;
}

struct yetty_ygui_widget *yetty_ygui_widget_tree_node_children(struct yetty_ygui_widget *node)
{
    if (!node || node->type != YETTY_YGUI_WIDGET_TREE_NODE) {
        return NULL;
    }
    return node->data.tree_node.children_list;
}

void yetty_ygui_widget_tree_node_on_toggle(struct yetty_ygui_widget *node, ygui_check_callback_t cb,
                                           void *userdata)
{
    if (!node || node->type != YETTY_YGUI_WIDGET_TREE_NODE) {
        return;
    }
    node->data.tree_node.on_toggle = cb;
    node->data.tree_node.on_toggle_userdata = userdata;
}

/*=============================================================================
 * Radio group / Radio button
 *
 * RADIO_GROUP is a flex-column container that tracks a selected child
 * radio. RADIO is a button-shaped widget with an outlined circle on
 * the left and a label to the right; clicking a radio asks its group
 * to make it the selection.
 *
 * The group fires change_callback on selection change with the new
 * index as the float value (-1 = none).
 *===========================================================================*/

static int radio_group_index_of(const struct yetty_ygui_widget *group,
                                const struct yetty_ygui_widget *child)
{
    int i = 0;
    for (struct yetty_ygui_widget *c = group->first_child; c; c = c->next_sibling) {
        if (c == child) {
            return i;
        }
        i++;
    }
    return -1;
}

static struct yetty_ygui_widget *radio_group_child_at(struct yetty_ygui_widget *group, int index)
{
    if (index < 0) {
        return NULL;
    }
    int i = 0;
    for (struct yetty_ygui_widget *c = group->first_child; c; c = c->next_sibling) {
        if (i == index) {
            return c;
        }
        i++;
    }
    return NULL;
}

/* Mark prior + new selection dirty so both repaint, then fire the
 * group's change_callback with the new index. */
static void radio_group_select(struct yetty_ygui_widget *group, struct yetty_ygui_widget *newsel)
{
    struct yetty_ygui_widget *prev = group->data.radio_group.selected;
    if (prev == newsel) {
        return;
    }
    if (prev) {
        prev->dirty = 1;
    }
    if (newsel) {
        newsel->dirty = 1;
    }
    group->data.radio_group.selected = newsel;
    group->dirty = 1;
    if (group->engine) {
        group->engine->dirty = 1;
    }
    if (group->change_callback) {
        int idx = newsel ? radio_group_index_of(group, newsel) : -1;
        group->change_callback(group, (float)idx, group->change_userdata);
    }
}

static struct yetty_ycore_void_result radio_render(struct yetty_ygui_widget *self,
                                                   struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float dia = ygui_max(12.0f, self->h - t->pad_small * 2);
    float cx = self->x + t->pad_small + dia * 0.5f;
    float cy = self->y + self->h * 0.5f;

    /* Outline */
    yetty_ygui_render_ctx_render_circle_outline(ctx, cx, cy, dia * 0.5f, t->border, 1.5f);

    /* Filled inner dot when selected. */
    struct yetty_ygui_widget *group = self->data.radio.group;
    int selected = (group && group->data.radio_group.selected == self);
    if (selected) {
        yetty_ygui_render_ctx_render_circle(ctx, cx, cy, dia * 0.30f, self->accent_color);
    }

    if (self->data.radio.label) {
        float text_x = self->x + t->pad_small + dia + t->pad_medium;
        yetty_ygui_render_ctx_render_text(ctx, self->data.radio.label, text_x,
                                          self->y + t->pad_medium, self->fg_color, t->font_size);
    }
    return YETTY_OK_VOID();
}

static int radio_on_release(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    if (lx < 0 || lx >= self->w || ly < 0 || ly >= self->h) {
        return 0;
    }
    if (self->data.radio.group) {
        radio_group_select(self->data.radio.group, self);
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.bool_value = 1;
    return 1;
}

static void radio_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.radio.label);
}

struct yetty_ygui_widget *yetty_ygui_engine_radio_group(struct yetty_ygui_engine *engine,
                                                        const char *id, float x, float y, float w,
                                                        float h)
{
    struct yetty_ygui_widget *g =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_RADIO_GROUP, id);
    if (!g) {
        return NULL;
    }
    yetty_ygui_widget_init_base(g, x, y, w, h);
    g->data.radio_group.selected = NULL;
    /* Default to flex column so radios stack vertically without the
     * caller having to apply CSS. Authors can override with row layout. */
    g->layout.mode = YETTY_YGUI_LAYOUT_FLEX;
    g->layout.direction = YETTY_YGUI_FLEX_COLUMN;
    g->layout.gap = 4.0f;
    g->layout.align_items = YETTY_YGUI_ALIGN_STRETCH;
    /* No vtable — group has no per-instance render (children paint
     * themselves; default render_all walks the child chain). */
    add_to_engine(engine, g);
    return g;
}

struct yetty_ygui_widget *yetty_ygui_widget_radio_group_add(struct yetty_ygui_widget *group,
                                                            const char *id, const char *label)
{
    if (!group || group->type != YETTY_YGUI_WIDGET_RADIO_GROUP || !group->engine) {
        return NULL;
    }
    struct yetty_ygui_widget *r =
        yetty_ygui_engine_widget_alloc(group->engine, YETTY_YGUI_WIDGET_RADIO, id);
    if (!r) {
        return NULL;
    }
    /* Sensible default size — caller can override via set_size / CSS. */
    yetty_ygui_widget_init_base(r, 0, 0, group->w, 24);
    r->data.radio.label = ygui_strdup(label);
    r->data.radio.group = group;
    static const struct yetty_ygui_widget_vtable radio_vtable = {
        .render = radio_render,
        .on_release = radio_on_release,
        .destroy = radio_destroy,
    };
    r->vtable = &radio_vtable;
    add_to_engine(group->engine, r);
    yetty_ygui_widget_add_child(group, r);
    return r;
}

void yetty_ygui_widget_radio_group_set_selected_index(struct yetty_ygui_widget *group, int index)
{
    if (!group || group->type != YETTY_YGUI_WIDGET_RADIO_GROUP) {
        return;
    }
    radio_group_select(group, index >= 0 ? radio_group_child_at(group, index) : NULL);
}

int yetty_ygui_widget_radio_group_get_selected_index(const struct yetty_ygui_widget *group)
{
    if (!group || group->type != YETTY_YGUI_WIDGET_RADIO_GROUP) {
        return -1;
    }
    return group->data.radio_group.selected
               ? radio_group_index_of(group, group->data.radio_group.selected)
               : -1;
}

void yetty_ygui_widget_radio_group_on_change(struct yetty_ygui_widget *group,
                                             ygui_change_callback_t cb, void *userdata)
{
    if (!group || group->type != YETTY_YGUI_WIDGET_RADIO_GROUP) {
        return;
    }
    group->change_callback = cb;
    group->change_userdata = userdata;
}

/*=============================================================================
 * Spinner — numeric input with ± buttons.
 *
 * Layout (left to right): [-]  value-text  [+]
 * Buttons are square, height = widget h, width = h. The middle area
 * shows the formatted value. Click left button to step down, right to
 * step up. Wheel and Up/Down keys also step. The widget clamps to
 * [min_val, max_val] and fires change_callback on every successful
 * change.
 *===========================================================================*/

static float spinner_btn_w(const struct yetty_ygui_widget *self)
{
    return self->h;
}

static void spinner_format(const struct yetty_ygui_widget *self, char *out, size_t cap)
{
    int p = self->data.spinner.precision;
    if (p < 0) p = 0;
    if (p > 6) p = 6;
    char fmt[16];
    snprintf(fmt, sizeof(fmt), "%%.%df", p);
    snprintf(out, cap, fmt, self->data.spinner.value);
}

static void spinner_apply(struct yetty_ygui_widget *self, float new_val)
{
    new_val = ygui_clamp(new_val, self->data.spinner.min_val, self->data.spinner.max_val);
    if (new_val == self->data.spinner.value) {
        return;
    }
    self->data.spinner.value = new_val;
    self->dirty = 1;
    if (self->engine) {
        self->engine->dirty = 1;
    }
    if (self->change_callback) {
        self->change_callback(self, new_val, self->change_userdata);
    }
}

static struct yetty_ycore_void_result spinner_render(struct yetty_ygui_widget *self,
                                                     struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float bw = spinner_btn_w(self);

    /* Frame */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, self->bg_color,
                                     t->radius_small);
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h, t->border,
                                             t->radius_small, 1.0f);

    /* Minus / plus button surfaces */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, bw, self->h, t->bg_secondary,
                                     t->radius_small);
    yetty_ygui_render_ctx_render_box(ctx, self->x + self->w - bw, self->y, bw, self->h,
                                     t->bg_secondary, t->radius_small);
    /* Minus glyph */
    yetty_ygui_render_ctx_render_box(ctx, self->x + bw * 0.25f, self->y + self->h * 0.5f - 1.0f,
                                     bw * 0.5f, 2.0f, self->fg_color, 1.0f);
    /* Plus glyph */
    yetty_ygui_render_ctx_render_box(ctx, self->x + self->w - bw + bw * 0.25f,
                                     self->y + self->h * 0.5f - 1.0f, bw * 0.5f, 2.0f,
                                     self->fg_color, 1.0f);
    yetty_ygui_render_ctx_render_box(ctx, self->x + self->w - bw + bw * 0.5f - 1.0f,
                                     self->y + self->h * 0.25f, 2.0f, self->h * 0.5f,
                                     self->fg_color, 1.0f);

    /* Value text */
    char buf[64];
    spinner_format(self, buf, sizeof(buf));
    yetty_ygui_render_ctx_render_text(ctx, buf, self->x + bw + t->pad_medium,
                                      self->y + t->pad_medium, self->fg_color, t->font_size);
    return YETTY_OK_VOID();
}

static int spinner_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)ly;
    float bw = spinner_btn_w(self);
    if (lx < bw) {
        spinner_apply(self, self->data.spinner.value - self->data.spinner.step);
    } else if (lx >= self->w - bw) {
        spinner_apply(self, self->data.spinner.value + self->data.spinner.step);
    } else {
        return 0;
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.float_value = self->data.spinner.value;
    return 1;
}

static int spinner_on_scroll(struct yetty_ygui_widget *self, float dx, float dy, ygui_event_t *out)
{
    (void)dx;
    if (dy == 0) {
        return 0;
    }
    float prev = self->data.spinner.value;
    spinner_apply(self, prev + (dy > 0 ? self->data.spinner.step : -self->data.spinner.step));
    if (self->data.spinner.value == prev) {
        return 0;
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_SCROLL;
    out->data.scroll.x = 0;
    out->data.scroll.y = self->data.spinner.value;
    return 1;
}

/* KEY_UP=0x1000, KEY_DOWN=0x1001 — same convention demos use. */
static int spinner_on_key(struct yetty_ygui_widget *self, uint32_t key, int mods,
                          ygui_event_t *out)
{
    (void)mods;
    float prev = self->data.spinner.value;
    if (key == 0x1000) {
        spinner_apply(self, prev + self->data.spinner.step);
    } else if (key == 0x1001) {
        spinner_apply(self, prev - self->data.spinner.step);
    } else {
        return 0;
    }
    if (self->data.spinner.value == prev) {
        return 0;
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.float_value = self->data.spinner.value;
    return 1;
}

struct yetty_ygui_widget *yetty_ygui_engine_spinner(struct yetty_ygui_engine *engine,
                                                    const char *id, float x, float y, float w,
                                                    float h, float min_val, float max_val,
                                                    float step, float value)
{
    struct yetty_ygui_widget *s =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_SPINNER, id);
    if (!s) {
        return NULL;
    }
    yetty_ygui_widget_init_base(s, x, y, w, h);
    s->data.spinner.min_val = min_val;
    s->data.spinner.max_val = max_val;
    s->data.spinner.step = step != 0 ? step : 1.0f;
    s->data.spinner.precision = 0;
    s->data.spinner.value = ygui_clamp(value, min_val, max_val);
    static const struct yetty_ygui_widget_vtable spinner_vtable = {
        .render = spinner_render,
        .on_press = spinner_on_press,
        .on_scroll = spinner_on_scroll,
        .on_key = spinner_on_key,
    };
    s->vtable = &spinner_vtable;
    add_to_engine(engine, s);
    return s;
}

void yetty_ygui_widget_spinner_set_value(struct yetty_ygui_widget *widget, float value)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SPINNER) {
        return;
    }
    spinner_apply(widget, value);
}

float yetty_ygui_widget_spinner_get_value(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SPINNER) {
        return 0;
    }
    return widget->data.spinner.value;
}

void yetty_ygui_widget_spinner_set_range(struct yetty_ygui_widget *widget, float min_val,
                                         float max_val)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SPINNER) {
        return;
    }
    widget->data.spinner.min_val = min_val;
    widget->data.spinner.max_val = max_val;
    spinner_apply(widget, widget->data.spinner.value);
}

void yetty_ygui_widget_spinner_set_step(struct yetty_ygui_widget *widget, float step)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SPINNER) {
        return;
    }
    widget->data.spinner.step = step != 0 ? step : 1.0f;
}

void yetty_ygui_widget_spinner_set_precision(struct yetty_ygui_widget *widget, int decimals)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SPINNER) {
        return;
    }
    widget->data.spinner.precision = decimals;
    widget->dirty = 1;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

void yetty_ygui_widget_spinner_on_change(struct yetty_ygui_widget *widget,
                                         ygui_change_callback_t cb, void *userdata)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SPINNER) {
        return;
    }
    widget->change_callback = cb;
    widget->change_userdata = userdata;
}

/*=============================================================================
 * Splitter — drag-to-resize divider.
 *===========================================================================*/

static int splitter_axis_row(const struct yetty_ygui_widget *self)
{
    /* Explicit override wins — set by external-drive callers that pin
     * the splitter over a non-flex region (yui's pane tree). */
    if (self->data.splitter.axis_override == 0 ||
        self->data.splitter.axis_override == 1) {
        return self->data.splitter.axis_override;
    }
    return self->parent && self->parent->layout.mode == YETTY_YGUI_LAYOUT_FLEX
               ? self->parent->layout.direction == YETTY_YGUI_FLEX_ROW
               : 1; /* default: assume row (vertical bar) */
}

static struct yetty_ycore_void_result splitter_render(struct yetty_ygui_widget *self,
                                                      struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    uint32_t bar = (self->flags & (YETTY_YGUI_FLAG_HOVER | YETTY_YGUI_FLAG_PRESSED))
                       ? t->thumb_hover
                       : t->border;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, bar, 0);
    return YETTY_OK_VOID();
}

static int splitter_on_drag(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    int row = splitter_axis_row(self);
    float delta = row ? (lx - self->w * 0.5f) : (ly - self->h * 0.5f);

    /* External-drive mode: forward delta, let the host decide. The
     * host re-positions this widget on the next frame; we do not
     * mutate any flex siblings. */
    if (self->change_callback) {
        self->change_callback(self, delta, self->change_userdata);
        self->dirty = 1;
        if (self->engine) {
            self->engine->dirty = 1;
        }
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_CHANGE;
        return 1;
    }

    struct yetty_ygui_widget *left = self->prev_sibling;
    struct yetty_ygui_widget *right = self->next_sibling;
    if (!left || !right || !self->parent) {
        return 0;
    }
    float min_sz = self->data.splitter.min_size > 0 ? self->data.splitter.min_size : 30.0f;
    if (row) {
        float new_l = left->authored_w + delta;
        float new_r = right->authored_w - delta;
        if (new_l < min_sz || new_r < min_sz) {
            return 0;
        }
        left->authored_w = new_l;
        right->authored_w = new_r;
    } else {
        float new_l = left->authored_h + delta;
        float new_r = right->authored_h - delta;
        if (new_l < min_sz || new_r < min_sz) {
            return 0;
        }
        left->authored_h = new_l;
        right->authored_h = new_r;
    }
    left->dirty = 1;
    right->dirty = 1;
    self->dirty = 1;
    if (self->engine) {
        self->engine->dirty = 1;
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    return 1;
}

static int splitter_on_press(struct yetty_ygui_widget *self, float lx, float ly,
                             ygui_event_t *out)
{
    (void)self; (void)lx; (void)ly; (void)out;
    /* Press alone doesn't move; engine sets PRESSED flag so render
     * highlights immediately. Drag will pick up from here. */
    return 0;
}

struct yetty_ygui_widget *yetty_ygui_engine_splitter(struct yetty_ygui_engine *engine,
                                                     const char *id, float x, float y, float w,
                                                     float h)
{
    struct yetty_ygui_widget *s =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_SPLITTER, id);
    if (!s) {
        return NULL;
    }
    yetty_ygui_widget_init_base(s, x, y, w, h);
    s->data.splitter.min_size = 30.0f;
    s->data.splitter.axis_override = -1;
    static const struct yetty_ygui_widget_vtable splitter_vtable = {
        .render = splitter_render,
        .on_press = splitter_on_press,
        .on_drag = splitter_on_drag,
    };
    s->vtable = &splitter_vtable;
    add_to_engine(engine, s);
    return s;
}

void yetty_ygui_widget_splitter_set_min(struct yetty_ygui_widget *widget, float min_size)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SPLITTER) {
        return;
    }
    widget->data.splitter.min_size = min_size;
}

void yetty_ygui_widget_splitter_on_change(struct yetty_ygui_widget *widget,
                                          ygui_change_callback_t cb, void *userdata)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SPLITTER) {
        return;
    }
    widget->change_callback = cb;
    widget->change_userdata = userdata;
}

void yetty_ygui_widget_splitter_set_axis(struct yetty_ygui_widget *widget, int row)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SPLITTER) {
        return;
    }
    widget->data.splitter.axis_override = row ? 1 : 0;
}

int yetty_ygui_widget_splitter_get_axis(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SPLITTER) {
        return -1;
    }
    return widget->data.splitter.axis_override;
}

int yetty_ygui_widget_get_type(const struct yetty_ygui_widget *widget)
{
    return widget ? (int)widget->type : -1;
}

/*=============================================================================
 * Modal dialog — popup + auto button row.
 *
 * Helper that lays out: title bar (popup's existing label area), a
 * message body, and a bottom row of buttons. Each button gets a
 * trampoline click handler that closes the dialog and calls the
 * user's on_button(dialog, index, userdata).
 *
 * The popup's lifecycle (visibility via OPEN flag, destruction via
 * engine teardown) is unchanged — this function just assembles the
 * subtree.
 *===========================================================================*/

struct dialog_button_ud {
    struct yetty_ygui_widget *dialog;
    int                       index;
    yetty_ygui_dialog_button_fn cb;
    void                     *userdata;
};

static void dialog_button_click(struct yetty_ygui_widget *btn, void *u)
{
    (void)btn;
    struct dialog_button_ud *ud = (struct dialog_button_ud *)u;
    yetty_ygui_widget_popup_set_open(ud->dialog, 0);
    if (ud->cb) {
        ud->cb(ud->dialog, ud->index, ud->userdata);
    }
}

/* Sized to be visually balanced for typical short labels. The popup's
 * own layout passes do the heavy lifting; these are sensible defaults
 * the caller can override with apply_css. */
struct yetty_ygui_widget *yetty_ygui_engine_dialog(struct yetty_ygui_engine *engine,
                                                   const struct yetty_ygui_dialog_args *args)
{
    if (!engine || !args || !args->id || args->button_count <= 0 || !args->buttons) {
        return NULL;
    }

    float dlg_w = 420.0f, dlg_h = 200.0f;
    float dlg_x = (engine->width  > dlg_w ? (engine->width  - dlg_w) * 0.5f : 0);
    float dlg_y = (engine->height > dlg_h ? (engine->height - dlg_h) * 0.5f : 0);

    struct yetty_ygui_widget *dlg = yetty_ygui_engine_popup(engine, args->id, dlg_x, dlg_y,
                                                            dlg_w, dlg_h,
                                                            args->title ? args->title : "");
    if (!dlg) {
        return NULL;
    }
    if (args->modal) {
        yetty_ygui_widget_popup_set_modal(dlg, 1);
    }

    /* Body container — vbox with the message and a button row. The
     * popup's title bar sits above (drawn by popup_render); children
     * live below it via padding-top from the popup body sizing. */
    struct yetty_ygui_widget *body = yetty_ygui_engine_vbox(engine, args->id, /*x*/ 16,
                                                            /*y*/ 56, dlg_w - 32, dlg_h - 72);
    yetty_ygui_widget_apply_css(body, "padding: 0; gap: 16; align-items: stretch;");
    yetty_ygui_widget_add_child(dlg, body);

    if (args->message) {
        struct yetty_ygui_widget *msg = yetty_ygui_engine_label(engine, args->id, 0, 0,
                                                                args->message);
        yetty_ygui_widget_add_child(body, msg);
    }

    /* Button row, right-aligned. */
    struct yetty_ygui_widget *row = yetty_ygui_engine_hbox(engine, args->id, 0, 0,
                                                           dlg_w - 32, 36);
    yetty_ygui_widget_apply_css(row,
                                "padding: 0; gap: 8; justify-content: end; align-items: stretch;");
    yetty_ygui_widget_add_child(body, row);

    for (int i = 0; i < args->button_count; i++) {
        const char *label = args->buttons[i] ? args->buttons[i] : "OK";
        struct yetty_ygui_widget *b = yetty_ygui_engine_button(engine, args->id, 0, 0, 88, 32,
                                                               label);
        struct dialog_button_ud *ud =
            (struct dialog_button_ud *)calloc(1, sizeof(*ud));
        if (!ud) {
            continue;
        }
        ud->dialog = dlg;
        ud->index = i;
        ud->cb = args->on_button;
        ud->userdata = args->userdata;
        yetty_ygui_widget_button_on_click(b, dialog_button_click, ud);
        yetty_ygui_widget_add_child(row, b);
    }

    return dlg;
}

/*=============================================================================
 * Multi-line text area.
 *
 * v1 scope: cursor + typing + Backspace + Enter + arrow keys +
 * Home/End + viewport line scroll. No selection, no clipboard, no
 * word wrap.
 *===========================================================================*/

#define TEXTAREA_KEY_LEFT  0x1002
#define TEXTAREA_KEY_RIGHT 0x1003
#define TEXTAREA_KEY_UP    0x1000
#define TEXTAREA_KEY_DOWN  0x1001
#define TEXTAREA_KEY_HOME  0x1006
#define TEXTAREA_KEY_END   0x1007
#define TEXTAREA_KEY_BACKSPACE 0x08
#define TEXTAREA_KEY_DELETE    0x7F
#define TEXTAREA_KEY_ENTER     0x0D
#define TEXTAREA_LINE_HEIGHT   18.0f
#define TEXTAREA_CHAR_WIDTH    8.0f  /* monospaced approximation */
#define TEXTAREA_PAD           6.0f

/* Allocate-or-grow the text buffer to at least new_size bytes (excluding
 * the NUL terminator). Returns 0 on success, -1 on OOM. */
static int textarea_reserve(struct yetty_ygui_widget *w, int new_size)
{
    if (w->data.textarea.text) {
        char *g = (char *)realloc(w->data.textarea.text, (size_t)new_size + 1);
        if (!g) {
            return -1;
        }
        w->data.textarea.text = g;
    } else {
        w->data.textarea.text = (char *)malloc((size_t)new_size + 1);
        if (!w->data.textarea.text) {
            return -1;
        }
        w->data.textarea.text[0] = '\0';
    }
    return 0;
}

void yetty_ygui_internal_textarea_insert(struct yetty_ygui_widget *w, const char *text);
void yetty_ygui_internal_textarea_insert(struct yetty_ygui_widget *w, const char *text)
{
    if (!w || !text || w->type != YETTY_YGUI_WIDGET_TEXTAREA) {
        return;
    }
    int add = (int)strlen(text);
    if (add == 0) {
        return;
    }
    if (textarea_reserve(w, w->data.textarea.length + add) < 0) {
        return;
    }
    if (w->data.textarea.cursor < 0) w->data.textarea.cursor = 0;
    if (w->data.textarea.cursor > w->data.textarea.length) {
        w->data.textarea.cursor = w->data.textarea.length;
    }
    /* Shift tail right, insert at cursor. */
    char *t = w->data.textarea.text;
    int c = w->data.textarea.cursor;
    int tail = w->data.textarea.length - c;
    if (tail > 0) {
        memmove(t + c + add, t + c, (size_t)tail);
    }
    memcpy(t + c, text, (size_t)add);
    w->data.textarea.length += add;
    t[w->data.textarea.length] = '\0';
    w->data.textarea.cursor += add;
    w->dirty = 1;
    if (w->engine) {
        w->engine->dirty = 1;
    }
    if (w->text_callback) {
        w->text_callback(w, t, w->text_userdata);
    }
}

static void textarea_delete_back(struct yetty_ygui_widget *w)
{
    if (w->data.textarea.cursor <= 0 || w->data.textarea.length == 0) {
        return;
    }
    char *t = w->data.textarea.text;
    int c = w->data.textarea.cursor;
    memmove(t + c - 1, t + c, (size_t)(w->data.textarea.length - c));
    w->data.textarea.length--;
    w->data.textarea.cursor--;
    t[w->data.textarea.length] = '\0';
    w->dirty = 1;
    if (w->engine) {
        w->engine->dirty = 1;
    }
    if (w->text_callback) {
        w->text_callback(w, t, w->text_userdata);
    }
}

static void textarea_delete_forward(struct yetty_ygui_widget *w)
{
    if (w->data.textarea.cursor >= w->data.textarea.length) {
        return;
    }
    char *t = w->data.textarea.text;
    int c = w->data.textarea.cursor;
    memmove(t + c, t + c + 1, (size_t)(w->data.textarea.length - c - 1));
    w->data.textarea.length--;
    t[w->data.textarea.length] = '\0';
    w->dirty = 1;
    if (w->engine) {
        w->engine->dirty = 1;
    }
    if (w->text_callback) {
        w->text_callback(w, t, w->text_userdata);
    }
}

/* Return (line, col) for the cursor and total line count. col is in
 * bytes within the current line, NOT screen columns. */
static void textarea_cursor_pos(const struct yetty_ygui_widget *w, int *out_line, int *out_col)
{
    int line = 0, col = 0;
    const char *t = w->data.textarea.text;
    int len = w->data.textarea.length;
    int c = w->data.textarea.cursor;
    if (c > len) c = len;
    for (int i = 0; i < c; i++) {
        if (t[i] == '\n') { line++; col = 0; }
        else              { col++; }
    }
    if (out_line) *out_line = line;
    if (out_col)  *out_col = col;
}

/* Byte offset of the start of line `line`. */
static int textarea_line_start(const struct yetty_ygui_widget *w, int line)
{
    int cur = 0;
    const char *t = w->data.textarea.text;
    int len = w->data.textarea.length;
    if (!t || line == 0) return 0;
    for (int i = 0; i < len; i++) {
        if (t[i] == '\n') {
            cur++;
            if (cur == line) return i + 1;
        }
    }
    return len;
}

/* Length of `line` in bytes (excluding the terminating \n). */
static int textarea_line_len(const struct yetty_ygui_widget *w, int line)
{
    int start = textarea_line_start(w, line);
    const char *t = w->data.textarea.text;
    int len = w->data.textarea.length;
    int i = start;
    while (i < len && t[i] != '\n') i++;
    return i - start;
}

static int textarea_total_lines(const struct yetty_ygui_widget *w)
{
    int lines = 1;
    const char *t = w->data.textarea.text;
    int len = w->data.textarea.length;
    for (int i = 0; i < len; i++) {
        if (t[i] == '\n') lines++;
    }
    return lines;
}

/* Adjust scroll_line so the cursor is on-screen. */
static void textarea_ensure_cursor_visible(struct yetty_ygui_widget *w)
{
    int line, col;
    textarea_cursor_pos(w, &line, &col);
    int viewport_lines = (int)((w->h - TEXTAREA_PAD * 2) / TEXTAREA_LINE_HEIGHT);
    if (viewport_lines < 1) viewport_lines = 1;
    if (line < w->data.textarea.scroll_line) {
        w->data.textarea.scroll_line = line;
    } else if (line >= w->data.textarea.scroll_line + viewport_lines) {
        w->data.textarea.scroll_line = line - viewport_lines + 1;
    }
}

static int textarea_on_key(struct yetty_ygui_widget *self, uint32_t key, int mods,
                           ygui_event_t *out)
{
    (void)mods;
    int handled = 1;
    int len = self->data.textarea.length;
    int *c = &self->data.textarea.cursor;
    const char *t = self->data.textarea.text;

    switch (key) {
    case TEXTAREA_KEY_BACKSPACE: textarea_delete_back(self); break;
    case TEXTAREA_KEY_DELETE:    textarea_delete_forward(self); break;
    case TEXTAREA_KEY_ENTER:     yetty_ygui_internal_textarea_insert(self, "\n"); break;
    case TEXTAREA_KEY_LEFT:      if (*c > 0) (*c)--; break;
    case TEXTAREA_KEY_RIGHT:     if (*c < len) (*c)++; break;
    case TEXTAREA_KEY_HOME: {
        int line, col;
        textarea_cursor_pos(self, &line, &col);
        *c = textarea_line_start(self, line);
        break;
    }
    case TEXTAREA_KEY_END: {
        int line, col;
        textarea_cursor_pos(self, &line, &col);
        *c = textarea_line_start(self, line) + textarea_line_len(self, line);
        break;
    }
    case TEXTAREA_KEY_UP: {
        int line, col;
        textarea_cursor_pos(self, &line, &col);
        if (line == 0) break;
        int prev_len = textarea_line_len(self, line - 1);
        int newcol = col < prev_len ? col : prev_len;
        *c = textarea_line_start(self, line - 1) + newcol;
        break;
    }
    case TEXTAREA_KEY_DOWN: {
        int line, col;
        textarea_cursor_pos(self, &line, &col);
        int total = textarea_total_lines(self);
        if (line + 1 >= total) break;
        int next_len = textarea_line_len(self, line + 1);
        int newcol = col < next_len ? col : next_len;
        *c = textarea_line_start(self, line + 1) + newcol;
        break;
    }
    default: handled = 0; break;
    }

    if (handled) {
        textarea_ensure_cursor_visible(self);
        self->dirty = 1;
        if (self->engine) self->engine->dirty = 1;
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_KEY;
        out->data.key.key = key;
        out->data.key.mods = mods;
        (void)t;
    }
    return handled;
}

static int textarea_on_press(struct yetty_ygui_widget *self, float lx, float ly,
                             ygui_event_t *out)
{
    /* Position the cursor at the click. Monospaced approximation —
     * good enough for v1; selection / pixel-accurate hit testing land
     * with the proper text-measurement integration. */
    (void)out;
    float fy = ly - TEXTAREA_PAD;
    if (fy < 0) fy = 0;
    int line_idx = (int)(fy / TEXTAREA_LINE_HEIGHT) + self->data.textarea.scroll_line;
    int total = textarea_total_lines(self);
    if (line_idx >= total) line_idx = total - 1;
    if (line_idx < 0) line_idx = 0;
    float fx = lx - TEXTAREA_PAD;
    if (fx < 0) fx = 0;
    int col = (int)(fx / TEXTAREA_CHAR_WIDTH);
    int line_len = textarea_line_len(self, line_idx);
    if (col > line_len) col = line_len;
    self->data.textarea.cursor = textarea_line_start(self, line_idx) + col;
    self->dirty = 1;
    if (self->engine) self->engine->dirty = 1;
    return 0; /* let the engine still take focus */
}

/* Render one visual line of the textarea, after applying right-edge
 * clipping. `max_cols` is the maximum char count that fits inside the
 * widget's inner width. `src` is the source bytes (no NUL); we copy
 * up to min(src_len, max_cols) into a stack buffer, NUL-terminate, and
 * emit one TEXT_SPAN. This guarantees no glyph is painted past
 * self->x + self->w - PAD. */
static void textarea_paint_line(struct yetty_ygui_widget *self,
                                struct yetty_ygui_render_ctx *ctx,
                                const char *src, int src_len, int max_cols, float row_y)
{
    if (src_len <= 0 || max_cols <= 0) {
        return;
    }
    int n = src_len < max_cols ? src_len : max_cols;
    char tmp[1024];
    if (n >= (int)sizeof(tmp)) n = (int)sizeof(tmp) - 1;
    memcpy(tmp, src, (size_t)n);
    tmp[n] = '\0';
    yetty_ygui_render_ctx_render_text(ctx, tmp, self->x + TEXTAREA_PAD, row_y, self->fg_color,
                                      ctx->theme->font_size);
}

/* Word-wrap one logical line. Walks `src[0..src_len)` and yields at
 * most one visual line per call. Returns the byte index where the
 * caller should resume on the next call (== src_len when the logical
 * line is consumed). Break points: the last space inside [0, max_cols),
 * or max_cols itself when the token is wider than the line. */
static int textarea_wrap_next(const char *src, int src_len, int from, int max_cols, int *out_end)
{
    int remaining = src_len - from;
    if (remaining <= max_cols) {
        *out_end = src_len;
        return src_len;
    }
    /* Find the last space inside the window. */
    int break_at = -1;
    for (int i = from + max_cols; i > from; i--) {
        if (src[i] == ' ' || src[i] == '\t') {
            break_at = i;
            break;
        }
    }
    if (break_at < 0) {
        /* Token wider than the line — hard break at max_cols. */
        *out_end = from + max_cols;
        return from + max_cols;
    }
    *out_end = break_at;
    /* Skip the space at the break point on resume. */
    return break_at + 1;
}

static struct yetty_ycore_void_result textarea_render(struct yetty_ygui_widget *self,
                                                      struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, self->bg_color,
                                     t->radius_small);
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h,
                                             (self->flags & YETTY_YGUI_FLAG_FOCUSED)
                                                 ? self->accent_color
                                                 : t->border,
                                             t->radius_small, 1.0f);

    /* Inner width in pixels and the corresponding character cap. The
     * cap is the source-of-truth for both the no-wrap truncation and
     * the wrap algorithm — it guarantees no glyph is painted outside
     * the widget's surface. */
    float inner_w = self->w - 2.0f * TEXTAREA_PAD;
    if (inner_w < TEXTAREA_CHAR_WIDTH) {
        inner_w = TEXTAREA_CHAR_WIDTH;
    }
    int max_cols = (int)(inner_w / TEXTAREA_CHAR_WIDTH);
    if (max_cols < 1) {
        max_cols = 1;
    }

    if (!self->data.textarea.text || self->data.textarea.length == 0) {
        /* Just paint the cursor when empty so the user knows it's focused. */
        if (self->flags & YETTY_YGUI_FLAG_FOCUSED) {
            yetty_ygui_render_ctx_render_box(ctx, self->x + TEXTAREA_PAD,
                                             self->y + TEXTAREA_PAD, 2.0f, TEXTAREA_LINE_HEIGHT,
                                             self->fg_color, 1.0f);
        }
        return YETTY_OK_VOID();
    }

    int viewport_lines = (int)((self->h - TEXTAREA_PAD * 2) / TEXTAREA_LINE_HEIGHT);
    if (viewport_lines < 1) viewport_lines = 1;
    int first = self->data.textarea.scroll_line;
    int total = textarea_total_lines(self);
    int last = first + viewport_lines;
    if (last > total) last = total;

    int rows_emitted = 0;
    for (int li = first; li < last && rows_emitted < viewport_lines; li++) {
        int start = textarea_line_start(self, li);
        int llen = textarea_line_len(self, li);
        if (llen <= 0) {
            rows_emitted++;
            continue;
        }
        const char *line = self->data.textarea.text + start;

        if (!self->data.textarea.wrap) {
            /* No wrap — truncate at max_cols (clips at the right edge). */
            float row_y = self->y + TEXTAREA_PAD + rows_emitted * TEXTAREA_LINE_HEIGHT + 2.0f;
            textarea_paint_line(self, ctx, line, llen, max_cols, row_y);
            rows_emitted++;
        } else {
            /* Wrap — emit one visual sub-row per call into the wrap
             * iterator until the logical line is fully consumed or the
             * viewport fills. */
            int pos = 0;
            while (pos < llen && rows_emitted < viewport_lines) {
                int seg_end = 0;
                int next_pos = textarea_wrap_next(line, llen, pos, max_cols, &seg_end);
                float row_y = self->y + TEXTAREA_PAD + rows_emitted * TEXTAREA_LINE_HEIGHT + 2.0f;
                textarea_paint_line(self, ctx, line + pos, seg_end - pos, max_cols, row_y);
                rows_emitted++;
                pos = next_pos;
            }
        }
    }

    /* Cursor — only in no-wrap mode (the wrap algorithm has no inverse
     * mapping from byte offset to visual row yet). */
    if (!self->data.textarea.wrap && (self->flags & YETTY_YGUI_FLAG_FOCUSED)) {
        int cline, ccol;
        textarea_cursor_pos(self, &cline, &ccol);
        if (cline >= first && cline < last) {
            if (ccol > max_cols) {
                ccol = max_cols; /* clip cursor to the right edge */
            }
            float cx = self->x + TEXTAREA_PAD + ccol * TEXTAREA_CHAR_WIDTH;
            float cy = self->y + TEXTAREA_PAD + (cline - first) * TEXTAREA_LINE_HEIGHT;
            yetty_ygui_render_ctx_render_box(ctx, cx, cy, 2.0f, TEXTAREA_LINE_HEIGHT,
                                             self->fg_color, 1.0f);
        }
    }
    return YETTY_OK_VOID();
}

static void textarea_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.textarea.text);
    self->data.textarea.text = NULL;
}

struct yetty_ygui_widget *yetty_ygui_engine_textarea(struct yetty_ygui_engine *engine,
                                                     const char *id, float x, float y, float w,
                                                     float h, const char *initial_text)
{
    struct yetty_ygui_widget *ta =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_TEXTAREA, id);
    if (!ta) {
        return NULL;
    }
    yetty_ygui_widget_init_base(ta, x, y, w, h);
    if (initial_text && *initial_text) {
        int n = (int)strlen(initial_text);
        ta->data.textarea.text = (char *)malloc((size_t)n + 1);
        if (ta->data.textarea.text) {
            memcpy(ta->data.textarea.text, initial_text, (size_t)n + 1);
            ta->data.textarea.length = n;
            ta->data.textarea.cursor = n;
        }
    }
    ta->data.textarea.wrap = 0;
    static const struct yetty_ygui_widget_vtable textarea_vtable = {
        .render = textarea_render,
        .on_press = textarea_on_press,
        .on_key = textarea_on_key,
        .destroy = textarea_destroy,
    };
    ta->vtable = &textarea_vtable;
    add_to_engine(engine, ta);
    return ta;
}

struct yetty_ygui_widget *yetty_ygui_engine_textarea_wrapped(struct yetty_ygui_engine *engine,
                                                             const char *id, float x, float y,
                                                             float w, float h,
                                                             const char *initial_text)
{
    struct yetty_ygui_widget *ta =
        yetty_ygui_engine_textarea(engine, id, x, y, w, h, initial_text);
    if (ta) {
        ta->data.textarea.wrap = 1;
    }
    return ta;
}

void yetty_ygui_widget_textarea_set_wrap(struct yetty_ygui_widget *widget, int wrap)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TEXTAREA) {
        return;
    }
    widget->data.textarea.wrap = wrap ? 1 : 0;
    widget->dirty = 1;
    if (widget->engine) {
        widget->engine->dirty = 1;
    }
}

void yetty_ygui_widget_textarea_set_text(struct yetty_ygui_widget *widget, const char *text)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TEXTAREA) {
        return;
    }
    free(widget->data.textarea.text);
    widget->data.textarea.text = NULL;
    widget->data.textarea.length = 0;
    widget->data.textarea.cursor = 0;
    widget->data.textarea.scroll_line = 0;
    if (text && *text) {
        int n = (int)strlen(text);
        widget->data.textarea.text = (char *)malloc((size_t)n + 1);
        if (widget->data.textarea.text) {
            memcpy(widget->data.textarea.text, text, (size_t)n + 1);
            widget->data.textarea.length = n;
            widget->data.textarea.cursor = n;
        }
    }
    widget->dirty = 1;
    if (widget->engine) widget->engine->dirty = 1;
}

const char *yetty_ygui_widget_textarea_get_text(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TEXTAREA) {
        return NULL;
    }
    return widget->data.textarea.text;
}

void yetty_ygui_widget_textarea_on_change(struct yetty_ygui_widget *widget,
                                          ygui_text_callback_t cb, void *userdata)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TEXTAREA) {
        return;
    }
    widget->text_callback = cb;
    widget->text_userdata = userdata;
}

/*=============================================================================
 * Scrollarea — vertical scrolling container.
 *
 * Renders children offset by -scroll_y. Critically, render_all also
 * patches every descendant's layout_y by -scroll_y BEFORE returning,
 * so the spatial grid (rebuilt after render in engine_rebuild) sees
 * the on-screen positions and routes clicks correctly even when the
 * content is scrolled.
 *===========================================================================*/

static void scrollarea_shift_subtree_y(struct yetty_ygui_widget *w, float dy)
{
    w->layout_y += dy;
    for (struct yetty_ygui_widget *c = w->first_child; c; c = c->next_sibling) {
        scrollarea_shift_subtree_y(c, dy);
    }
}

/* Compute total content height from children — used by the scrollable
 * vtable when content_h was not set explicitly. The flex preflight
 * pass already sized each child to its intrinsic height, so we just
 * find the bottom edge among visible children. */
static float scrollarea_compute_content_h(const struct yetty_ygui_widget *self)
{
    if (self->data.scrollarea.content_h > 0.0f) {
        return self->data.scrollarea.content_h;
    }
    float bottom = 0.0f;
    for (struct yetty_ygui_widget *c = self->first_child; c; c = c->next_sibling) {
        if (!(c->flags & YETTY_YGUI_FLAG_VISIBLE)) {
            continue;
        }
        float b = c->y + c->h;
        if (b > bottom) bottom = b;
    }
    return bottom;
}

static float scrollarea_max_scroll(const struct yetty_ygui_widget *self)
{
    float content_h = scrollarea_compute_content_h(self);
    float vp = self->layout_h;
    return content_h > vp ? content_h - vp : 0.0f;
}

void yetty_ygui_widget_scrollarea_scroll_to(struct yetty_ygui_widget *widget, float y)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SCROLLAREA) {
        return;
    }
    float max_s = scrollarea_max_scroll(widget);
    float prev = widget->data.scrollarea.scroll_y;
    widget->data.scrollarea.scroll_y = ygui_clamp(y, 0.0f, max_s);
    if (widget->data.scrollarea.scroll_y == prev) {
        return;
    }
    widget->dirty = 1;
    if (widget->engine) widget->engine->dirty = 1;
    if (widget->scroll_observer) widget->scroll_observer->dirty = 1;
}

void yetty_ygui_widget_scrollarea_scroll_by(struct yetty_ygui_widget *widget, float dy)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SCROLLAREA) {
        return;
    }
    yetty_ygui_widget_scrollarea_scroll_to(widget, widget->data.scrollarea.scroll_y + dy);
}

float yetty_ygui_widget_scrollarea_get_scroll(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_SCROLLAREA) {
        return 0.0f;
    }
    return widget->data.scrollarea.scroll_y;
}

/* Scrollable interface — lets vscrollbar_bind drive the area. */
static float scrollarea_get_content(const struct yetty_ygui_widget *w)
{
    return scrollarea_compute_content_h(w);
}
static float scrollarea_get_viewport(const struct yetty_ygui_widget *w) { return w->layout_h; }
static float scrollarea_get_scroll(const struct yetty_ygui_widget *w)
{
    return w->data.scrollarea.scroll_y;
}
static float scrollarea_get_max_scroll(const struct yetty_ygui_widget *w)
{
    return scrollarea_max_scroll(w);
}
static void  scrollarea_scroll_to_iface(struct yetty_ygui_widget *w, float y)
{
    yetty_ygui_widget_scrollarea_scroll_to(w, y);
}

static const struct yetty_ygui_scrollable *scrollarea_iface(void)
{
    static const struct yetty_ygui_scrollable ops = {
        .get_content_h = scrollarea_get_content,
        .get_viewport_h = scrollarea_get_viewport,
        .get_scroll = scrollarea_get_scroll,
        .get_max_scroll = scrollarea_get_max_scroll,
        .scroll_to = scrollarea_scroll_to_iface,
    };
    return &ops;
}

static int scrollarea_on_scroll(struct yetty_ygui_widget *self, float dx, float dy,
                                ygui_event_t *out)
{
    (void)dx;
    const float speed = 40.0f;
    float prev = self->data.scrollarea.scroll_y;
    yetty_ygui_widget_scrollarea_scroll_to(self, prev - dy * speed);
    if (self->data.scrollarea.scroll_y == prev) {
        return 0;
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_SCROLL;
    out->data.scroll.x = 0;
    out->data.scroll.y = self->data.scrollarea.scroll_y;
    return 1;
}

static struct yetty_ycore_void_result scrollarea_render(struct yetty_ygui_widget *self,
                                                        struct yetty_ygui_render_ctx *ctx)
{
    /* No background by default — just clip area logically (no real
     * GPU clipping yet; off-viewport children still emit but the
     * receiver layer occludes them naturally). */
    (void)self; (void)ctx;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result scrollarea_render_all(struct yetty_ygui_widget *self,
                                                            struct yetty_ygui_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_FLAG_VISIBLE)) {
        return YETTY_OK_VOID();
    }
    self->was_rendered = 1;

    /* Clamp scroll on every render so resizing the viewport (and thus
     * shrinking max_scroll) doesn't strand us past the new bottom. */
    float max_s = scrollarea_max_scroll(self);
    if (self->data.scrollarea.scroll_y > max_s) {
        self->data.scrollarea.scroll_y = max_s;
    }
    float scroll = self->data.scrollarea.scroll_y;

    /* Open the scrollarea's CMD_GROUP (parent-relative rect), then
     * recurse children INSIDE the open scope. Each child's CMD_GROUP
     * rect carries its own y; we mutate child->y by -scroll for the
     * recursion (and restore after) so the emitted rect reflects the
     * scrolled position. */
    uint32_t marker = yetty_ygui_widget_open_group(self, ctx, scrollarea_render);

    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    for (struct yetty_ygui_widget *child = self->first_child; child;
         child = child->next_sibling) {
        if (!(child->flags & YETTY_YGUI_FLAG_VISIBLE)) {
            continue;
        }
        float saved_y = (float)child->y;
        child->y = (int)(saved_y - scroll);
        struct yetty_ycore_void_result r;
        if (child->vtable && child->vtable->render_all) {
            r = child->vtable->render_all(child, ctx);
        } else {
            r = yetty_ygui_widget_render_all_default(child, ctx);
        }
        child->y = (int)saved_y;
        if (YETTY_IS_ERR(r)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = r;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
    }

    /* Patch every descendant's layout_y by -scroll so the spatial
     * grid sees the on-screen positions. The grid rebuild runs
     * AFTER render. */
    if (scroll != 0.0f) {
        for (struct yetty_ygui_widget *child = self->first_child; child;
             child = child->next_sibling) {
            scrollarea_shift_subtree_y(child, -scroll);
        }
    }

    yetty_ygui_widget_close_group(self, ctx, marker);
    return first_err;
}

struct yetty_ygui_widget *yetty_ygui_engine_scrollarea(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y,
                                                       float w, float h)
{
    struct yetty_ygui_widget *s =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_SCROLLAREA, id);
    if (!s) {
        return NULL;
    }
    yetty_ygui_widget_init_base(s, x, y, w, h);
    s->data.scrollarea.scroll_y = 0;
    s->data.scrollarea.content_h = 0; /* 0 = auto */
    /* Flex column by default so children stack vertically and inherit
     * the viewport width via align-items: stretch. */
    s->layout.mode = YETTY_YGUI_LAYOUT_FLEX;
    s->layout.direction = YETTY_YGUI_FLEX_COLUMN;
    s->layout.align_items = YETTY_YGUI_ALIGN_STRETCH;
    s->layout.gap = 4.0f;
    static const struct yetty_ygui_widget_vtable scrollarea_vtable = {
        .render = scrollarea_render,
        .render_all = scrollarea_render_all,
        .on_scroll = scrollarea_on_scroll,
    };
    s->vtable = &scrollarea_vtable;
    s->scrollable = scrollarea_iface();
    add_to_engine(engine, s);
    return s;
}

/*=============================================================================
 * Toggle switch — pill-shaped on/off control.
 *===========================================================================*/

static struct yetty_ycore_void_result toggle_render(struct yetty_ygui_widget *self,
                                                    struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float pill_w = ygui_max(36.0f, self->h * 1.8f);
    float pill_h = self->h - 4.0f;
    if (pill_h < 14.0f) pill_h = 14.0f;
    float pill_x = self->x;
    float pill_y = self->y + (self->h - pill_h) * 0.5f;

    /* Off-state pill needs to be visible against the section's body
     * frame (which uses bg_secondary). Use bg_primary (the darkest
     * brand surface) + a visible outline so the affordance is obvious
     * even when off. On-state stays solid accent. */
    uint32_t track = self->data.toggle.on ? self->accent_color : t->bg_primary;
    yetty_ygui_render_ctx_render_box(ctx, pill_x, pill_y, pill_w, pill_h, track, pill_h * 0.5f);
    /* Always draw a border so the pill outline is visible against any
     * surrounding surface. Slightly brighter when off, accent-coloured
     * when on. */
    uint32_t border = self->data.toggle.on ? self->accent_color : t->border;
    yetty_ygui_render_ctx_render_box_outline(ctx, pill_x, pill_y, pill_w, pill_h, border,
                                             pill_h * 0.5f, 1.5f);

    float thumb_d = pill_h - 4.0f;
    float thumb_y = pill_y + 2.0f;
    float thumb_x = self->data.toggle.on
                        ? pill_x + pill_w - thumb_d - 2.0f
                        : pill_x + 2.0f;
    /* Off-state thumb uses text_muted (visible against dark pill);
     * on-state uses pure white-ish for contrast against the accent. */
    uint32_t thumb_col = self->data.toggle.on ? t->text_primary : t->text_muted;
    yetty_ygui_render_ctx_render_box(ctx, thumb_x, thumb_y, thumb_d, thumb_d,
                                     thumb_col, thumb_d * 0.5f);

    if (self->data.toggle.label) {
        yetty_ygui_render_ctx_render_text(ctx, self->data.toggle.label,
                                          pill_x + pill_w + t->pad_medium,
                                          self->y + t->pad_medium, self->fg_color,
                                          t->font_size);
    }
    return YETTY_OK_VOID();
}

static int toggle_on_release(struct yetty_ygui_widget *self, float lx, float ly,
                             ygui_event_t *out)
{
    if (lx < 0 || lx >= self->w || ly < 0 || ly >= self->h) {
        return 0;
    }
    self->data.toggle.on = !self->data.toggle.on;
    if (self->check_callback) {
        self->check_callback(self, self->data.toggle.on, self->check_userdata);
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.bool_value = self->data.toggle.on;
    return 1;
}

static void toggle_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.toggle.label);
}

struct yetty_ygui_widget *yetty_ygui_engine_toggle(struct yetty_ygui_engine *engine,
                                                   const char *id, float x, float y,
                                                   float w, float h, const char *label, int on)
{
    struct yetty_ygui_widget *t =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_TOGGLE, id);
    if (!t) {
        return NULL;
    }
    yetty_ygui_widget_init_base(t, x, y, w, h);
    t->data.toggle.on = on ? 1 : 0;
    t->data.toggle.label = ygui_strdup(label);
    static const struct yetty_ygui_widget_vtable toggle_vtable = {
        .render = toggle_render,
        .on_release = toggle_on_release,
        .destroy = toggle_destroy,
    };
    t->vtable = &toggle_vtable;
    add_to_engine(engine, t);
    return t;
}

void yetty_ygui_widget_toggle_set_on(struct yetty_ygui_widget *widget, int on)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TOGGLE) return;
    if (widget->data.toggle.on == (on ? 1 : 0)) return;
    widget->data.toggle.on = on ? 1 : 0;
    widget->dirty = 1;
    if (widget->engine) widget->engine->dirty = 1;
}

int yetty_ygui_widget_toggle_get_on(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TOGGLE) return 0;
    return widget->data.toggle.on;
}

void yetty_ygui_widget_toggle_on_change(struct yetty_ygui_widget *widget,
                                        ygui_check_callback_t cb, void *userdata)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_TOGGLE) return;
    widget->check_callback = cb;
    widget->check_userdata = userdata;
}

/*=============================================================================
 * Chip / tag — small pill label with optional ✕ close button.
 *===========================================================================*/

static float chip_close_w(const struct yetty_ygui_widget *self)
{
    return self->data.chip.closable ? self->h : 0.0f;
}

static struct yetty_ycore_void_result chip_render(struct yetty_ygui_widget *self,
                                                  struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h,
                                     t->bg_secondary, self->h * 0.5f);
    if (self->data.chip.label) {
        yetty_ygui_render_ctx_render_text(ctx, self->data.chip.label,
                                          self->x + self->h * 0.5f,
                                          self->y + t->pad_small, self->fg_color, t->font_size);
    }
    if (self->data.chip.closable) {
        float cw = chip_close_w(self);
        float cx = self->x + self->w - cw * 0.5f;
        float cy = self->y + self->h * 0.5f;
        /* Two crossed diagonals, approximated with thin boxes. */
        float s = cw * 0.25f;
        yetty_ygui_render_ctx_render_box(ctx, cx - s, cy - 1.0f, s * 2, 2.0f, self->fg_color, 1);
        yetty_ygui_render_ctx_render_box(ctx, cx - 1.0f, cy - s, 2.0f, s * 2, self->fg_color, 1);
    }
    return YETTY_OK_VOID();
}

static int chip_on_release(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    if (!self->data.chip.closable) {
        return 0;
    }
    float cw = chip_close_w(self);
    if (lx >= self->w - cw && lx < self->w && ly >= 0 && ly < self->h) {
        if (self->data.chip.on_remove) {
            self->data.chip.on_remove(self, self->data.chip.on_remove_userdata);
        }
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_CLICK;
        return 1;
    }
    return 0;
}

static void chip_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.chip.label);
}

struct yetty_ygui_widget *yetty_ygui_engine_chip(struct yetty_ygui_engine *engine,
                                                 const char *id, float x, float y,
                                                 float w, float h, const char *label, int closable)
{
    struct yetty_ygui_widget *c =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_CHIP, id);
    if (!c) {
        return NULL;
    }
    yetty_ygui_widget_init_base(c, x, y, w, h);
    c->data.chip.label = ygui_strdup(label);
    c->data.chip.closable = closable ? 1 : 0;
    c->data.chip.on_remove = NULL;
    c->data.chip.on_remove_userdata = NULL;
    static const struct yetty_ygui_widget_vtable chip_vtable = {
        .render = chip_render,
        .on_release = chip_on_release,
        .destroy = chip_destroy,
    };
    c->vtable = &chip_vtable;
    add_to_engine(engine, c);
    return c;
}

void yetty_ygui_widget_chip_on_remove(struct yetty_ygui_widget *widget,
                                      ygui_click_callback_t cb, void *userdata)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_CHIP) return;
    widget->data.chip.on_remove = cb;
    widget->data.chip.on_remove_userdata = userdata;
}

/*=============================================================================
 * Breadcrumbs — horizontal sequence of clickable segments separated by ' › '.
 *===========================================================================*/

#define BREADCRUMB_CHAR_W 8.0f
#define BREADCRUMB_SEP " > "
#define BREADCRUMB_SEP_LEN 3

static int breadcrumbs_seg_at(const struct yetty_ygui_widget *self, float lx)
{
    if (lx < 0) return -1;
    float cursor = 0;
    for (int i = 0; i < self->data.breadcrumbs.n; i++) {
        int len = self->data.breadcrumbs.labels[i]
                      ? (int)strlen(self->data.breadcrumbs.labels[i])
                      : 0;
        float seg_w = len * BREADCRUMB_CHAR_W;
        if (lx >= cursor && lx < cursor + seg_w) {
            return i;
        }
        cursor += seg_w;
        if (i + 1 < self->data.breadcrumbs.n) {
            cursor += BREADCRUMB_SEP_LEN * BREADCRUMB_CHAR_W;
        }
    }
    return -1;
}

static struct yetty_ycore_void_result breadcrumbs_render(struct yetty_ygui_widget *self,
                                                          struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    float cursor = self->x;
    for (int i = 0; i < self->data.breadcrumbs.n; i++) {
        const char *label = self->data.breadcrumbs.labels[i];
        if (!label) continue;
        uint32_t col = (i == self->data.breadcrumbs.n - 1) ? self->fg_color : t->text_muted;
        yetty_ygui_render_ctx_render_text(ctx, label, cursor, self->y + t->pad_small, col,
                                          t->font_size);
        cursor += strlen(label) * BREADCRUMB_CHAR_W;
        if (i + 1 < self->data.breadcrumbs.n) {
            yetty_ygui_render_ctx_render_text(ctx, BREADCRUMB_SEP, cursor,
                                              self->y + t->pad_small, t->text_muted, t->font_size);
            cursor += BREADCRUMB_SEP_LEN * BREADCRUMB_CHAR_W;
        }
    }
    return YETTY_OK_VOID();
}

static int breadcrumbs_on_release(struct yetty_ygui_widget *self, float lx, float ly,
                                  ygui_event_t *out)
{
    (void)ly;
    int idx = breadcrumbs_seg_at(self, lx);
    if (idx < 0) return 0;
    if (self->change_callback) {
        self->change_callback(self, (float)idx, self->change_userdata);
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.float_value = (float)idx;
    return 1;
}

static void breadcrumbs_destroy(struct yetty_ygui_widget *self)
{
    for (int i = 0; i < self->data.breadcrumbs.n; i++) {
        free(self->data.breadcrumbs.labels[i]);
    }
    free(self->data.breadcrumbs.labels);
}

struct yetty_ygui_widget *yetty_ygui_engine_breadcrumbs(struct yetty_ygui_engine *engine,
                                                        const char *id,
                                                        float x, float y, float w, float h,
                                                        const char *const *labels, int n)
{
    struct yetty_ygui_widget *b =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_BREADCRUMBS, id);
    if (!b) return NULL;
    yetty_ygui_widget_init_base(b, x, y, w, h);
    b->data.breadcrumbs.n = n;
    b->data.breadcrumbs.labels = NULL;
    if (n > 0) {
        b->data.breadcrumbs.labels = (char **)calloc((size_t)n, sizeof(char *));
        if (b->data.breadcrumbs.labels) {
            for (int i = 0; i < n; i++) {
                b->data.breadcrumbs.labels[i] = ygui_strdup(labels[i]);
            }
        } else {
            b->data.breadcrumbs.n = 0;
        }
    }
    static const struct yetty_ygui_widget_vtable breadcrumbs_vtable = {
        .render = breadcrumbs_render,
        .on_release = breadcrumbs_on_release,
        .destroy = breadcrumbs_destroy,
    };
    b->vtable = &breadcrumbs_vtable;
    add_to_engine(engine, b);
    return b;
}

void yetty_ygui_widget_breadcrumbs_on_change(struct yetty_ygui_widget *widget,
                                             ygui_change_callback_t cb, void *userdata)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_BREADCRUMBS) return;
    widget->change_callback = cb;
    widget->change_userdata = userdata;
}

/*=============================================================================
 * Indeterminate progress
 *
 * Stored as a flag + an animation phase that the render path advances
 * each frame. Renders a small accent-coloured slug travelling across
 * the track.
 *===========================================================================*/

void yetty_ygui_widget_progress_set_indeterminate(struct yetty_ygui_widget *widget,
                                                  int indeterminate)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_PROGRESS) return;
    widget->data.progress.indeterminate = indeterminate ? 1 : 0;
    widget->dirty = 1;
    if (widget->engine) widget->engine->dirty = 1;
}

/*=============================================================================
 * Right-click context menu wiring.
 *
 * Pure plumbing: widgets store an optional `context_menu` pointer.
 * The engine's mouse_down dispatch (in ygui_engine.c) checks for
 * right-click and opens the menu at the cursor BEFORE invoking the
 * widget's on_press.
 *===========================================================================*/

void yetty_ygui_widget_set_context_menu(struct yetty_ygui_widget *widget,
                                        struct yetty_ygui_widget *menu)
{
    if (!widget) return;
    widget->context_menu = menu;
}

/*=============================================================================
 * Combo box — editable textinput with a dropdown of suggestions.
 *===========================================================================*/

static float combo_arrow_w(const struct yetty_ygui_widget *self) { return self->h; }

static struct yetty_ycore_void_result combo_render(struct yetty_ygui_widget *self,
                                                   struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, self->bg_color,
                                     t->radius_small);
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h,
                                             (self->flags & YETTY_YGUI_FLAG_FOCUSED)
                                                 ? self->accent_color
                                                 : t->border,
                                             t->radius_small, 1.0f);
    /* Text */
    const char *txt = self->data.combo.text ? self->data.combo.text : "";
    yetty_ygui_render_ctx_render_text(ctx, txt, self->x + 8.0f,
                                      self->y + (self->h - t->font_size) * 0.5f,
                                      self->fg_color, t->font_size);
    /* Arrow button */
    float aw = combo_arrow_w(self);
    float ax = self->x + self->w - aw;
    yetty_ygui_render_ctx_render_box(ctx, ax, self->y, aw, self->h, t->bg_secondary,
                                     t->radius_small);
    float cx = ax + aw * 0.5f;
    float cy = self->y + self->h * 0.5f;
    yetty_ygui_render_ctx_render_triangle(ctx, cx - 5, cy - 3, cx + 5, cy - 3, cx, cy + 4,
                                          self->fg_color);
    /* Dropdown list (when open) */
    if (self->data.combo.dropdown_open && self->data.combo.option_count > 0) {
        float row_h = self->h;
        float dy = self->y + self->h + 2;
        float dh = row_h * self->data.combo.option_count;
        yetty_ygui_render_ctx_render_box(ctx, self->x, dy, self->w, dh, t->bg_dropdown,
                                         t->radius_small);
        yetty_ygui_render_ctx_render_box_outline(ctx, self->x, dy, self->w, dh, t->border,
                                                 t->radius_small, 1.0f);
        for (int i = 0; i < self->data.combo.option_count; i++) {
            yetty_ygui_render_ctx_render_text(ctx, self->data.combo.options[i],
                                              self->x + 8.0f,
                                              dy + i * row_h + (row_h - t->font_size) * 0.5f,
                                              self->fg_color, t->font_size);
        }
    }
    return YETTY_OK_VOID();
}

static int combo_on_release(struct yetty_ygui_widget *self, float lx, float ly,
                            ygui_event_t *out)
{
    float aw = combo_arrow_w(self);
    /* Click on arrow: toggle dropdown */
    if (lx >= self->w - aw && lx < self->w && ly >= 0 && ly < self->h) {
        self->data.combo.dropdown_open = !self->data.combo.dropdown_open;
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_CHANGE;
        return 1;
    }
    /* Click in the dropdown area: pick that option */
    if (self->data.combo.dropdown_open && ly >= self->h + 2 && lx >= 0 && lx < self->w) {
        float row_h = self->h;
        int idx = (int)((ly - self->h - 2) / row_h);
        if (idx >= 0 && idx < self->data.combo.option_count) {
            free(self->data.combo.text);
            self->data.combo.text = ygui_strdup(self->data.combo.options[idx]);
            self->data.combo.dropdown_open = 0;
            if (self->text_callback) {
                self->text_callback(self, self->data.combo.text, self->text_userdata);
            }
            out->widget_id = self->id;
            out->type = YETTY_YGUI_EVENT_CHANGE;
            out->data.string_value = self->data.combo.text;
            return 1;
        }
    }
    /* Click in field: focus, close dropdown */
    self->data.combo.dropdown_open = 0;
    return 0;
}

static void combo_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.combo.text);
    for (int i = 0; i < self->data.combo.option_count; i++) {
        free(self->data.combo.options[i]);
    }
    free(self->data.combo.options);
}

struct yetty_ygui_widget *yetty_ygui_engine_combo(struct yetty_ygui_engine *engine,
                                                  const char *id, float x, float y,
                                                  float w, float h, const char *initial_text,
                                                  const char *const *options, int option_count)
{
    struct yetty_ygui_widget *c =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_COMBO, id);
    if (!c) return NULL;
    yetty_ygui_widget_init_base(c, x, y, w, h);
    c->data.combo.text = ygui_strdup(initial_text);
    c->data.combo.option_count = option_count;
    if (option_count > 0 && options) {
        c->data.combo.options = (char **)calloc((size_t)option_count, sizeof(char *));
        if (c->data.combo.options) {
            for (int i = 0; i < option_count; i++) {
                c->data.combo.options[i] = ygui_strdup(options[i]);
            }
        } else {
            c->data.combo.option_count = 0;
        }
    }
    static const struct yetty_ygui_widget_vtable combo_vtable = {
        .render = combo_render,
        .on_release = combo_on_release,
        .destroy = combo_destroy,
    };
    c->vtable = &combo_vtable;
    add_to_engine(engine, c);
    return c;
}

void yetty_ygui_widget_combo_set_text(struct yetty_ygui_widget *widget, const char *text)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COMBO) return;
    free(widget->data.combo.text);
    widget->data.combo.text = ygui_strdup(text);
    widget->dirty = 1;
    if (widget->engine) widget->engine->dirty = 1;
}

const char *yetty_ygui_widget_combo_get_text(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COMBO) return NULL;
    return widget->data.combo.text;
}

void yetty_ygui_widget_combo_on_change(struct yetty_ygui_widget *widget,
                                       ygui_text_callback_t cb, void *userdata)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_COMBO) return;
    widget->text_callback = cb;
    widget->text_userdata = userdata;
}

/*=============================================================================
 * Menubar — horizontal strip of menu buttons.
 *
 * Each entry stores a label + a borrowed popup_menu pointer.
 * Clicking a button opens its menu anchored beneath the click.
 *===========================================================================*/

#define MENUBAR_ITEM_PAD_X 12.0f
#define MENUBAR_CHAR_W 8.0f

static float menubar_item_w(const struct yetty_ygui_widget *self, int i)
{
    const char *l = self->data.menubar.labels[i];
    int n = l ? (int)strlen(l) : 0;
    return MENUBAR_ITEM_PAD_X * 2 + n * MENUBAR_CHAR_W;
}

static int menubar_item_at(const struct yetty_ygui_widget *self, float lx)
{
    float cx = 0;
    for (int i = 0; i < self->data.menubar.n; i++) {
        float w = menubar_item_w(self, i);
        if (lx >= cx && lx < cx + w) return i;
        cx += w;
    }
    return -1;
}

static struct yetty_ycore_void_result menubar_render(struct yetty_ygui_widget *self,
                                                     struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, t->bg_header,
                                     t->radius_small);
    float cx = self->x;
    for (int i = 0; i < self->data.menubar.n; i++) {
        float w = menubar_item_w(self, i);
        const char *l = self->data.menubar.labels[i];
        yetty_ygui_render_ctx_render_text(ctx, l ? l : "", cx + MENUBAR_ITEM_PAD_X,
                                          self->y + (self->h - t->font_size) * 0.5f,
                                          self->fg_color, t->font_size);
        cx += w;
    }
    return YETTY_OK_VOID();
}

static int menubar_on_release(struct yetty_ygui_widget *self, float lx, float ly,
                              ygui_event_t *out)
{
    (void)ly;
    int idx = menubar_item_at(self, lx);
    if (idx < 0 || !self->data.menubar.menus[idx]) return 0;
    struct yetty_ygui_widget *clicked = self->data.menubar.menus[idx];
    int was_open = yetty_ygui_widget_popup_menu_is_open(clicked);
    /* Close every sibling menu first — at most one menu in this bar
     * is open at a time. */
    for (int i = 0; i < self->data.menubar.n; i++) {
        if (self->data.menubar.menus[i] && self->data.menubar.menus[i] != clicked) {
            yetty_ygui_widget_popup_menu_close(self->data.menubar.menus[i]);
        }
    }
    if (was_open) {
        /* Clicking the same menu button again closes it. */
        yetty_ygui_widget_popup_menu_close(clicked);
    } else {
        float cx = self->layout_x;
        for (int i = 0; i < idx; i++) cx += menubar_item_w(self, i);
        yetty_ygui_widget_popup_menu_open_at(clicked, cx,
                                              self->layout_y + self->layout_h);
    }
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.int_value = idx;
    return 1;
}

static void menubar_destroy(struct yetty_ygui_widget *self)
{
    for (int i = 0; i < self->data.menubar.n; i++) free(self->data.menubar.labels[i]);
    free(self->data.menubar.labels);
    free(self->data.menubar.menus);
}

struct yetty_ygui_widget *yetty_ygui_engine_menubar(struct yetty_ygui_engine *engine,
                                                    const char *id, float x, float y,
                                                    float w, float h)
{
    struct yetty_ygui_widget *m =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_MENUBAR, id);
    if (!m) return NULL;
    yetty_ygui_widget_init_base(m, x, y, w, h);
    static const struct yetty_ygui_widget_vtable menubar_vtable = {
        .render = menubar_render,
        .on_release = menubar_on_release,
        .destroy = menubar_destroy,
    };
    m->vtable = &menubar_vtable;
    add_to_engine(engine, m);
    return m;
}

void yetty_ygui_widget_menubar_add(struct yetty_ygui_widget *menubar, const char *label,
                                   struct yetty_ygui_widget *menu)
{
    if (!menubar || menubar->type != YETTY_YGUI_WIDGET_MENUBAR) return;
    if (menubar->data.menubar.n == menubar->data.menubar.capacity) {
        int nc = menubar->data.menubar.capacity ? menubar->data.menubar.capacity * 2 : 4;
        char **gl = realloc(menubar->data.menubar.labels, (size_t)nc * sizeof(char *));
        struct yetty_ygui_widget **gm =
            realloc(menubar->data.menubar.menus, (size_t)nc * sizeof(struct yetty_ygui_widget *));
        if (!gl || !gm) { free(gl); free(gm); return; }
        menubar->data.menubar.labels = gl;
        menubar->data.menubar.menus = gm;
        menubar->data.menubar.capacity = nc;
    }
    int i = menubar->data.menubar.n;
    menubar->data.menubar.labels[i] = ygui_strdup(label);
    menubar->data.menubar.menus[i] = menu;
    menubar->data.menubar.n++;
    menubar->dirty = 1;
    if (menubar->engine) menubar->engine->dirty = 1;
}

/*=============================================================================
 * Stepper — numbered-step indicator.
 *===========================================================================*/

#define STEPPER_CIRCLE_R 14.0f

static struct yetty_ycore_void_result stepper_render(struct yetty_ygui_widget *self,
                                                     struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    int n = self->data.stepper.n_steps;
    if (n <= 0) return YETTY_OK_VOID();
    float gap = (self->w - n * STEPPER_CIRCLE_R * 2) / (float)(n > 1 ? n - 1 : 1);
    if (gap < 0) gap = 0;
    float cy = self->y + STEPPER_CIRCLE_R + 4;
    float cx = self->x + STEPPER_CIRCLE_R;
    for (int i = 0; i < n; i++) {
        int complete = (i < self->data.stepper.current);
        int current  = (i == self->data.stepper.current);
        uint32_t fill = (complete || current) ? self->accent_color : t->bg_secondary;
        yetty_ygui_render_ctx_render_circle(ctx, cx, cy, STEPPER_CIRCLE_R, fill);
        if (current) {
            yetty_ygui_render_ctx_render_circle_outline(ctx, cx, cy, STEPPER_CIRCLE_R + 2,
                                                        self->accent_color, 2.0f);
        }
        /* Step number */
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", i + 1);
        yetty_ygui_render_ctx_render_text(ctx, buf, cx - 4, cy - t->font_size * 0.5f + 2,
                                          t->text_primary, t->font_size);
        /* Label below */
        const char *l = self->data.stepper.labels[i];
        if (l) {
            yetty_ygui_render_ctx_render_text(ctx, l,
                                              cx - (int)strlen(l) * 4.0f,
                                              cy + STEPPER_CIRCLE_R + 6,
                                              complete ? self->accent_color : t->text_muted,
                                              t->font_size);
        }
        /* Connecting line to next step */
        if (i + 1 < n) {
            uint32_t line_col = complete ? self->accent_color : t->border;
            float lx = cx + STEPPER_CIRCLE_R;
            float lw = gap;
            yetty_ygui_render_ctx_render_box(ctx, lx, cy - 1, lw, 2, line_col, 0);
        }
        cx += STEPPER_CIRCLE_R * 2 + gap;
    }
    return YETTY_OK_VOID();
}

static void stepper_destroy(struct yetty_ygui_widget *self)
{
    for (int i = 0; i < self->data.stepper.n_steps; i++) free(self->data.stepper.labels[i]);
    free(self->data.stepper.labels);
}

struct yetty_ygui_widget *yetty_ygui_engine_stepper(struct yetty_ygui_engine *engine,
                                                    const char *id, float x, float y,
                                                    float w, float h,
                                                    const char *const *labels, int n_steps)
{
    struct yetty_ygui_widget *s =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_STEPPER, id);
    if (!s) return NULL;
    yetty_ygui_widget_init_base(s, x, y, w, h);
    s->data.stepper.n_steps = n_steps;
    s->data.stepper.current = 0;
    if (n_steps > 0 && labels) {
        s->data.stepper.labels = (char **)calloc((size_t)n_steps, sizeof(char *));
        if (s->data.stepper.labels) {
            for (int i = 0; i < n_steps; i++) s->data.stepper.labels[i] = ygui_strdup(labels[i]);
        } else {
            s->data.stepper.n_steps = 0;
        }
    }
    static const struct yetty_ygui_widget_vtable stepper_vtable = {
        .render = stepper_render,
        .destroy = stepper_destroy,
    };
    s->vtable = &stepper_vtable;
    add_to_engine(engine, s);
    return s;
}

void yetty_ygui_widget_stepper_set_current(struct yetty_ygui_widget *widget, int step)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_STEPPER) return;
    if (step < 0) step = 0;
    if (step > widget->data.stepper.n_steps - 1) step = widget->data.stepper.n_steps - 1;
    if (widget->data.stepper.current == step) return;
    widget->data.stepper.current = step;
    widget->dirty = 1;
    if (widget->engine) widget->engine->dirty = 1;
}

int yetty_ygui_widget_stepper_get_current(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_STEPPER) return 0;
    return widget->data.stepper.current;
}

/*=============================================================================
 * Date picker — compact month calendar.
 *===========================================================================*/

static int days_in_month(int year, int m0)
{
    static const int dim[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m0 < 0 || m0 > 11) return 30;
    if (m0 != 1) return dim[m0];
    int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;
    return 28 + leap;
}

/* Zeller-style: 0 = Sunday, 1 = Monday, ..., 6 = Saturday. */
static int weekday_of(int year, int m0, int day)
{
    int m = m0 + 1;
    int y = year;
    if (m < 3) { m += 12; y -= 1; }
    int k = y % 100;
    int j = y / 100;
    int h = (day + (13 * (m + 1)) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;
    /* Zeller: 0=Saturday, 1=Sunday, 2=Monday, ..., 6=Friday → convert */
    return (h + 6) % 7;
}

#define DATEPICKER_HEADER_H 28.0f
#define DATEPICKER_CELL_W   28.0f
#define DATEPICKER_CELL_H   24.0f
#define DATEPICKER_PAD      6.0f
#define DATEPICKER_NAV_W    28.0f

static const char *month_name(int m0)
{
    static const char *names[] = {"January", "February", "March", "April", "May", "June",
                                  "July", "August", "September", "October", "November",
                                  "December"};
    if (m0 < 0 || m0 > 11) return "?";
    return names[m0];
}

static struct yetty_ycore_void_result datepicker_render(struct yetty_ygui_widget *self,
                                                        struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, self->bg_color,
                                     t->radius_small);
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h, t->border,
                                             t->radius_small, 1.0f);
    /* Header: ← Month Year → */
    /* Left arrow */
    yetty_ygui_render_ctx_render_text(ctx, "<", self->x + DATEPICKER_PAD,
                                      self->y + (DATEPICKER_HEADER_H - t->font_size) * 0.5f,
                                      self->fg_color, t->font_size);
    /* Right arrow */
    yetty_ygui_render_ctx_render_text(ctx, ">", self->x + self->w - DATEPICKER_NAV_W,
                                      self->y + (DATEPICKER_HEADER_H - t->font_size) * 0.5f,
                                      self->fg_color, t->font_size);
    /* Month + year */
    char hdr[64];
    snprintf(hdr, sizeof(hdr), "%s %d", month_name(self->data.datepicker.shown_month),
             self->data.datepicker.shown_year);
    yetty_ygui_render_ctx_render_text(ctx, hdr, self->x + DATEPICKER_NAV_W + DATEPICKER_PAD,
                                      self->y + (DATEPICKER_HEADER_H - t->font_size) * 0.5f,
                                      self->fg_color, t->font_size);

    /* Weekday header */
    static const char *wd[] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
    float grid_x = self->x + DATEPICKER_PAD;
    float grid_y = self->y + DATEPICKER_HEADER_H + 4;
    for (int i = 0; i < 7; i++) {
        yetty_ygui_render_ctx_render_text(ctx, wd[i], grid_x + i * DATEPICKER_CELL_W + 6,
                                          grid_y, t->text_muted, t->font_size);
    }
    /* Days */
    grid_y += DATEPICKER_CELL_H;
    int first_wd = weekday_of(self->data.datepicker.shown_year,
                               self->data.datepicker.shown_month, 1);
    int dim = days_in_month(self->data.datepicker.shown_year,
                             self->data.datepicker.shown_month);
    for (int d = 1; d <= dim; d++) {
        int cell = (first_wd + d - 1);
        int row = cell / 7;
        int col = cell % 7;
        float cx = grid_x + col * DATEPICKER_CELL_W;
        float cy = grid_y + row * DATEPICKER_CELL_H;
        int is_sel = (self->data.datepicker.sel_year == self->data.datepicker.shown_year &&
                      self->data.datepicker.sel_month == self->data.datepicker.shown_month &&
                      self->data.datepicker.sel_day == d);
        if (is_sel) {
            yetty_ygui_render_ctx_render_box(ctx, cx, cy, DATEPICKER_CELL_W,
                                             DATEPICKER_CELL_H, self->accent_color,
                                             t->radius_small);
        }
        char buf[8];
        snprintf(buf, sizeof(buf), "%d", d);
        yetty_ygui_render_ctx_render_text(ctx, buf, cx + 8, cy + 4,
                                          is_sel ? t->text_primary : self->fg_color, t->font_size);
    }
    return YETTY_OK_VOID();
}

static int datepicker_on_release(struct yetty_ygui_widget *self, float lx, float ly,
                                 ygui_event_t *out)
{
    /* Header arrows */
    if (ly < DATEPICKER_HEADER_H) {
        if (lx < DATEPICKER_NAV_W) {
            self->data.datepicker.shown_month--;
            if (self->data.datepicker.shown_month < 0) {
                self->data.datepicker.shown_month = 11;
                self->data.datepicker.shown_year--;
            }
        } else if (lx > self->w - DATEPICKER_NAV_W) {
            self->data.datepicker.shown_month++;
            if (self->data.datepicker.shown_month > 11) {
                self->data.datepicker.shown_month = 0;
                self->data.datepicker.shown_year++;
            }
        } else {
            return 0;
        }
        self->dirty = 1;
        if (self->engine) self->engine->dirty = 1;
        return 0;
    }
    /* Day grid */
    float grid_x = DATEPICKER_PAD;
    float grid_y = DATEPICKER_HEADER_H + 4 + DATEPICKER_CELL_H;
    if (ly < grid_y) return 0;
    int col = (int)((lx - grid_x) / DATEPICKER_CELL_W);
    int row = (int)((ly - grid_y) / DATEPICKER_CELL_H);
    if (col < 0 || col > 6 || row < 0 || row > 5) return 0;
    int first_wd = weekday_of(self->data.datepicker.shown_year,
                               self->data.datepicker.shown_month, 1);
    int dim = days_in_month(self->data.datepicker.shown_year,
                             self->data.datepicker.shown_month);
    int d = row * 7 + col - first_wd + 1;
    if (d < 1 || d > dim) return 0;
    self->data.datepicker.sel_year = self->data.datepicker.shown_year;
    self->data.datepicker.sel_month = self->data.datepicker.shown_month;
    self->data.datepicker.sel_day = d;
    int packed = self->data.datepicker.sel_year * 10000 +
                 (self->data.datepicker.sel_month + 1) * 100 +
                 self->data.datepicker.sel_day;
    if (self->change_callback) {
        self->change_callback(self, (float)packed, self->change_userdata);
    }
    self->dirty = 1;
    if (self->engine) self->engine->dirty = 1;
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.int_value = packed;
    return 1;
}

struct yetty_ygui_widget *yetty_ygui_engine_datepicker(struct yetty_ygui_engine *engine,
                                                       const char *id,
                                                       float x, float y, float w, float h)
{
    struct yetty_ygui_widget *d =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_DATEPICKER, id);
    if (!d) return NULL;
    yetty_ygui_widget_init_base(d, x, y, w, h);
    /* Default to today (compile-time fallback if no clock). */
    d->data.datepicker.shown_year = 2025;
    d->data.datepicker.shown_month = 0;
    d->data.datepicker.sel_year = -1;
    d->data.datepicker.sel_month = -1;
    d->data.datepicker.sel_day = -1;
    static const struct yetty_ygui_widget_vtable datepicker_vtable = {
        .render = datepicker_render,
        .on_release = datepicker_on_release,
    };
    d->vtable = &datepicker_vtable;
    add_to_engine(engine, d);
    return d;
}

void yetty_ygui_widget_datepicker_set_date(struct yetty_ygui_widget *widget, int year,
                                           int month_0_based, int day)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_DATEPICKER) return;
    widget->data.datepicker.shown_year = year;
    widget->data.datepicker.shown_month = month_0_based;
    widget->data.datepicker.sel_year = year;
    widget->data.datepicker.sel_month = month_0_based;
    widget->data.datepicker.sel_day = day;
    widget->dirty = 1;
    if (widget->engine) widget->engine->dirty = 1;
}

void yetty_ygui_widget_datepicker_get_date(const struct yetty_ygui_widget *widget, int *year,
                                           int *month_0_based, int *day)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_DATEPICKER) {
        if (year) *year = -1;
        if (month_0_based) *month_0_based = -1;
        if (day) *day = -1;
        return;
    }
    if (year) *year = widget->data.datepicker.sel_year;
    if (month_0_based) *month_0_based = widget->data.datepicker.sel_month;
    if (day) *day = widget->data.datepicker.sel_day;
}

void yetty_ygui_widget_datepicker_on_change(struct yetty_ygui_widget *widget,
                                            ygui_change_callback_t cb, void *userdata)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_DATEPICKER) return;
    widget->change_callback = cb;
    widget->change_userdata = userdata;
}

/*=============================================================================
 * File picker — directory listing widget.
 *===========================================================================*/

#include <yetty/yplatform/fs.h>

static int filepicker_entry_cmp(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void filepicker_load_dir(struct yetty_ygui_widget *self)
{
    for (int i = 0; i < self->data.filepicker.entry_count; i++) {
        free(self->data.filepicker.entries[i]);
    }
    free(self->data.filepicker.entries);
    self->data.filepicker.entries = NULL;
    self->data.filepicker.entry_count = 0;
    self->data.filepicker.selected = -1;
    self->data.filepicker.scroll = 0;

    struct yetty_yplatform_dir *d = yetty_yplatform_dir_open(self->data.filepicker.cwd);
    if (!d) return;
    int cap = 16, n = 0;
    char **list = (char **)malloc((size_t)cap * sizeof(char *));
    if (!list) { yetty_yplatform_dir_close(d); return; }
    struct yetty_yplatform_dir_entry e;
    while (yetty_yplatform_dir_next(d, &e)) {
        /* Skip "." but keep ".." for navigation. */
        if (strcmp(e.name, ".") == 0) continue;
        if (n == cap) {
            int nc = cap * 2;
            char **g = realloc(list, (size_t)nc * sizeof(char *));
            if (!g) break;
            list = g;
            cap = nc;
        }
        /* Append "/" suffix for directories so the user can see what's
         * navigable. */
        size_t name_len = strlen(e.name);
        char *entry = (char *)malloc(name_len + 2);
        if (!entry) continue;
        memcpy(entry, e.name, name_len);
        if (e.is_dir) { entry[name_len] = '/'; entry[name_len + 1] = '\0'; }
        else          { entry[name_len] = '\0'; }
        list[n++] = entry;
    }
    yetty_yplatform_dir_close(d);
    qsort(list, (size_t)n, sizeof(char *), filepicker_entry_cmp);
    self->data.filepicker.entries = list;
    self->data.filepicker.entry_count = n;
}

#define FILEPICKER_ROW_H 22.0f
#define FILEPICKER_HEADER_H 24.0f
#define FILEPICKER_PAD 8.0f

static struct yetty_ycore_void_result filepicker_render(struct yetty_ygui_widget *self,
                                                        struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, self->bg_color,
                                     t->radius_small);
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h, t->border,
                                             t->radius_small, 1.0f);
    /* Header: current path */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, FILEPICKER_HEADER_H,
                                     t->bg_header, t->radius_small);
    if (self->data.filepicker.cwd) {
        yetty_ygui_render_ctx_render_text(ctx, self->data.filepicker.cwd,
                                          self->x + FILEPICKER_PAD,
                                          self->y + (FILEPICKER_HEADER_H - t->font_size) * 0.5f,
                                          self->fg_color, t->font_size);
    }

    float list_y = self->y + FILEPICKER_HEADER_H + 2;
    float list_h = self->h - FILEPICKER_HEADER_H - 4;
    int visible = (int)(list_h / FILEPICKER_ROW_H);
    if (visible < 1) visible = 1;
    int n = self->data.filepicker.entry_count;
    int first = self->data.filepicker.scroll;
    int last = first + visible;
    if (last > n) last = n;
    for (int i = first; i < last; i++) {
        float ry = list_y + (i - first) * FILEPICKER_ROW_H;
        if (i == self->data.filepicker.selected) {
            yetty_ygui_render_ctx_render_box(ctx, self->x + 2, ry, self->w - 4,
                                             FILEPICKER_ROW_H, t->selection_bg, 0);
        }
        const char *e = self->data.filepicker.entries[i];
        uint32_t col = (e && strchr(e, '/')) ? self->accent_color : self->fg_color;
        yetty_ygui_render_ctx_render_text(ctx, e ? e : "", self->x + FILEPICKER_PAD,
                                          ry + (FILEPICKER_ROW_H - t->font_size) * 0.5f,
                                          col, t->font_size);
    }
    return YETTY_OK_VOID();
}

static int filepicker_on_scroll(struct yetty_ygui_widget *self, float dx, float dy,
                                ygui_event_t *out)
{
    (void)dx;
    int delta = dy > 0 ? -1 : 1;
    int prev = self->data.filepicker.scroll;
    self->data.filepicker.scroll += delta;
    if (self->data.filepicker.scroll < 0) self->data.filepicker.scroll = 0;
    int max_scroll = self->data.filepicker.entry_count -
                     (int)((self->h - FILEPICKER_HEADER_H - 4) / FILEPICKER_ROW_H);
    if (max_scroll < 0) max_scroll = 0;
    if (self->data.filepicker.scroll > max_scroll) self->data.filepicker.scroll = max_scroll;
    if (self->data.filepicker.scroll == prev) return 0;
    self->dirty = 1;
    if (self->engine) self->engine->dirty = 1;
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_SCROLL;
    return 1;
}

static int filepicker_on_release(struct yetty_ygui_widget *self, float lx, float ly,
                                 ygui_event_t *out)
{
    (void)lx;
    if (ly < FILEPICKER_HEADER_H + 2) return 0;
    int row = (int)((ly - FILEPICKER_HEADER_H - 2) / FILEPICKER_ROW_H) +
              self->data.filepicker.scroll;
    if (row < 0 || row >= self->data.filepicker.entry_count) return 0;
    const char *name = self->data.filepicker.entries[row];
    if (!name) return 0;
    int is_dir = strchr(name, '/') != NULL;
    if (is_dir) {
        /* Navigate */
        char newcwd[4096];
        if (strcmp(name, "../") == 0) {
            /* Up — strip last path component */
            strncpy(newcwd, self->data.filepicker.cwd, sizeof(newcwd) - 1);
            newcwd[sizeof(newcwd) - 1] = '\0';
            char *slash = strrchr(newcwd, '/');
            if (slash && slash != newcwd) *slash = '\0';
            else if (slash == newcwd) newcwd[1] = '\0'; /* keep "/" root */
        } else {
            size_t namelen = strlen(name);
            size_t cwdlen = strlen(self->data.filepicker.cwd);
            if (cwdlen + 1 + namelen >= sizeof(newcwd)) return 0;
            if (strcmp(self->data.filepicker.cwd, "/") == 0) {
                snprintf(newcwd, sizeof(newcwd), "/%s", name);
            } else {
                snprintf(newcwd, sizeof(newcwd), "%s/%s", self->data.filepicker.cwd, name);
            }
            /* Strip the trailing "/" we appended to dirs. */
            size_t L = strlen(newcwd);
            if (L > 1 && newcwd[L - 1] == '/') newcwd[L - 1] = '\0';
        }
        free(self->data.filepicker.cwd);
        self->data.filepicker.cwd = ygui_strdup(newcwd);
        filepicker_load_dir(self);
        self->dirty = 1;
        if (self->engine) self->engine->dirty = 1;
        out->widget_id = self->id;
        out->type = YETTY_YGUI_EVENT_CHANGE;
        return 1;
    }
    /* File — select */
    self->data.filepicker.selected = row;
    if (self->text_callback) {
        self->text_callback(self, name, self->text_userdata);
    }
    self->dirty = 1;
    if (self->engine) self->engine->dirty = 1;
    out->widget_id = self->id;
    out->type = YETTY_YGUI_EVENT_CHANGE;
    out->data.string_value = name;
    return 1;
}

static void filepicker_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.filepicker.cwd);
    for (int i = 0; i < self->data.filepicker.entry_count; i++) {
        free(self->data.filepicker.entries[i]);
    }
    free(self->data.filepicker.entries);
}

struct yetty_ygui_widget *yetty_ygui_engine_filepicker(struct yetty_ygui_engine *engine,
                                                       const char *id,
                                                       float x, float y, float w, float h,
                                                       const char *initial_dir)
{
    struct yetty_ygui_widget *f =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_FILEPICKER, id);
    if (!f) return NULL;
    yetty_ygui_widget_init_base(f, x, y, w, h);
    f->data.filepicker.cwd = ygui_strdup(initial_dir && *initial_dir ? initial_dir : "/");
    f->data.filepicker.selected = -1;
    filepicker_load_dir(f);
    static const struct yetty_ygui_widget_vtable filepicker_vtable = {
        .render = filepicker_render,
        .on_release = filepicker_on_release,
        .on_scroll = filepicker_on_scroll,
        .destroy = filepicker_destroy,
    };
    f->vtable = &filepicker_vtable;
    add_to_engine(engine, f);
    return f;
}

const char *yetty_ygui_widget_filepicker_get_cwd(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_FILEPICKER) return NULL;
    return widget->data.filepicker.cwd;
}

const char *yetty_ygui_widget_filepicker_get_selected(const struct yetty_ygui_widget *widget)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_FILEPICKER) return NULL;
    int s = widget->data.filepicker.selected;
    if (s < 0 || s >= widget->data.filepicker.entry_count) return NULL;
    return widget->data.filepicker.entries[s];
}

void yetty_ygui_widget_filepicker_on_change(struct yetty_ygui_widget *widget,
                                            ygui_text_callback_t cb, void *userdata)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_FILEPICKER) return;
    widget->text_callback = cb;
    widget->text_userdata = userdata;
}

/*=============================================================================
 * Statusbar — bottom-of-window strip.
 *===========================================================================*/

#define STATUSBAR_CHAR_W 8.0f
#define STATUSBAR_PAD_X 10.0f

static struct yetty_ycore_void_result statusbar_render(struct yetty_ygui_widget *self,
                                                       struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *t = ctx->theme;
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, t->bg_header,
                                     0.0f);
    /* Top separator line for visual delineation from the body. */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, 1.0f, t->border_muted, 0.0f);
    float ty = self->y + (self->h - t->font_size) * 0.5f;
    if (self->data.statusbar.left_text) {
        yetty_ygui_render_ctx_render_text(ctx, self->data.statusbar.left_text,
                                          self->x + STATUSBAR_PAD_X, ty,
                                          t->text_primary, t->font_size);
    }
    if (self->data.statusbar.right_text) {
        int len = (int)strlen(self->data.statusbar.right_text);
        float tx = self->x + self->w - STATUSBAR_PAD_X - len * STATUSBAR_CHAR_W;
        yetty_ygui_render_ctx_render_text(ctx, self->data.statusbar.right_text, tx, ty,
                                          t->text_muted, t->font_size);
    }
    return YETTY_OK_VOID();
}

static void statusbar_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.statusbar.left_text);
    free(self->data.statusbar.right_text);
}

struct yetty_ygui_widget *yetty_ygui_engine_statusbar(struct yetty_ygui_engine *engine,
                                                      const char *id, float x, float y,
                                                      float w, float h, const char *left_text)
{
    struct yetty_ygui_widget *s =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_STATUSBAR, id);
    if (!s) return NULL;
    yetty_ygui_widget_init_base(s, x, y, w, h);
    s->data.statusbar.left_text = ygui_strdup(left_text);
    s->data.statusbar.right_text = NULL;
    static const struct yetty_ygui_widget_vtable statusbar_vtable = {
        .render = statusbar_render,
        .destroy = statusbar_destroy,
    };
    s->vtable = &statusbar_vtable;
    add_to_engine(engine, s);
    return s;
}

void yetty_ygui_widget_statusbar_set_left(struct yetty_ygui_widget *widget, const char *text)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_STATUSBAR) return;
    free(widget->data.statusbar.left_text);
    widget->data.statusbar.left_text = ygui_strdup(text);
    widget->dirty = 1;
    if (widget->engine) widget->engine->dirty = 1;
}

void yetty_ygui_widget_statusbar_set_right(struct yetty_ygui_widget *widget, const char *text)
{
    if (!widget || widget->type != YETTY_YGUI_WIDGET_STATUSBAR) return;
    free(widget->data.statusbar.right_text);
    widget->data.statusbar.right_text = ygui_strdup(text);
    widget->dirty = 1;
    if (widget->engine) widget->engine->dirty = 1;
}

/*=============================================================================
 * Engine-wide bars — pin titlebar / menubar to the top and statusbar
 * to the bottom of the canvas. The pinning happens in
 * engine_pin_bars called at the head of the layout pass.
 *===========================================================================*/

void yetty_ygui_engine_set_titlebar(struct yetty_ygui_engine *engine,
                                    struct yetty_ygui_widget *widget)
{
    if (!engine) return;
    engine->engine_titlebar = widget;
    engine->dirty = 1;
}

void yetty_ygui_engine_set_menubar(struct yetty_ygui_engine *engine,
                                   struct yetty_ygui_widget *widget)
{
    if (!engine) return;
    engine->engine_menubar = widget;
    engine->dirty = 1;
}

void yetty_ygui_engine_set_statusbar(struct yetty_ygui_engine *engine,
                                     struct yetty_ygui_widget *widget)
{
    if (!engine) return;
    engine->engine_statusbar = widget;
    engine->dirty = 1;
}

/* Called from the layout pass head. Pins engine-level bar widgets
 * to top / bottom strips spanning the canvas width. The widgets'
 * authored heights are preserved; width is overridden to span the
 * canvas. */
void yetty_ygui_internal_engine_pin_bars(struct yetty_ygui_engine *engine);
void yetty_ygui_internal_engine_pin_bars(struct yetty_ygui_engine *engine)
{
    if (!engine) return;
    float W = engine->width;
    float H = engine->height;
    float top_y = 0.0f;
    if (engine->engine_titlebar) {
        struct yetty_ygui_widget *w = engine->engine_titlebar;
        float h = w->authored_h > 0 ? w->authored_h : 32.0f;
        w->authored_x = 0;
        w->authored_y = top_y;
        w->authored_w = W;
        w->authored_h = h;
        top_y += h;
    }
    if (engine->engine_menubar) {
        struct yetty_ygui_widget *w = engine->engine_menubar;
        float h = w->authored_h > 0 ? w->authored_h : 28.0f;
        w->authored_x = 0;
        w->authored_y = top_y;
        w->authored_w = W;
        w->authored_h = h;
        top_y += h;
    }
    if (engine->engine_statusbar) {
        struct yetty_ygui_widget *w = engine->engine_statusbar;
        float h = w->authored_h > 0 ? w->authored_h : 22.0f;
        w->authored_x = 0;
        w->authored_y = H - h;
        w->authored_w = W;
        w->authored_h = h;
    }
}

/*=============================================================================
 * Window: menubar + statusbar slots.
 *
 * The window is a flex-column container with this child order:
 *   [ <padding_top = title_h> ][ menubar? ][ body ][ statusbar? ]
 *
 * set_menubar inserts the widget right after the title-bar padding
 * (so it sits below the title), set_statusbar appends to the end
 * (flex layout pushes it against the bottom because body has
 * flex_grow=1).
 *
 * Both setters re-parent the widget; passing NULL detaches the
 * current slot widget back to the engine's top-level chain (so the
 * caller's pointer stays valid and the widget can be reattached
 * elsewhere or destroyed via the engine).
 *===========================================================================*/

static void window_detach_slot(struct yetty_ygui_widget *window, struct yetty_ygui_widget **slot)
{
    if (!*slot) return;
    yetty_ygui_widget_remove_child(window, *slot);
    /* Reinsert at engine top level so the widget remains tracked. */
    if (window->engine) {
        add_to_engine(window->engine, *slot);
    }
    *slot = NULL;
}

void yetty_ygui_widget_window_set_menubar(struct yetty_ygui_widget *window,
                                          struct yetty_ygui_widget *menubar)
{
    if (!window || window->type != YETTY_YGUI_WIDGET_WINDOW) return;
    window_detach_slot(window, &window->data.window.menubar);
    if (!menubar) return;
    /* Reparent menubar into the window. We want it BEFORE body in
     * child order so the flex column places it above. The body was
     * added in window's constructor; if it's still the only child,
     * inserting before it means swapping the head. The simpler
     * approach: remove body, append menubar, re-append body. */
    struct yetty_ygui_widget *body = window->data.window.body;
    struct yetty_ygui_widget *sb   = window->data.window.statusbar;
    if (body) yetty_ygui_widget_remove_child(window, body);
    if (sb)   yetty_ygui_widget_remove_child(window, sb);
    if (menubar->parent) {
        yetty_ygui_widget_remove_child(menubar->parent, menubar);
    }
    yetty_ygui_widget_add_child(window, menubar);
    if (body) yetty_ygui_widget_add_child(window, body);
    if (sb)   yetty_ygui_widget_add_child(window, sb);
    window->data.window.menubar = menubar;
    window->dirty = 1;
    if (window->engine) window->engine->dirty = 1;
}

void yetty_ygui_widget_window_set_statusbar(struct yetty_ygui_widget *window,
                                            struct yetty_ygui_widget *statusbar)
{
    if (!window || window->type != YETTY_YGUI_WIDGET_WINDOW) return;
    window_detach_slot(window, &window->data.window.statusbar);
    if (!statusbar) return;
    /* Statusbar goes at the END of the child list so flex stacks it
     * below body (which has flex_grow=1, so it occupies remaining
     * space leaving the statusbar at the bottom). */
    if (statusbar->parent) {
        yetty_ygui_widget_remove_child(statusbar->parent, statusbar);
    }
    yetty_ygui_widget_add_child(window, statusbar);
    window->data.window.statusbar = statusbar;
    window->dirty = 1;
    if (window->engine) window->engine->dirty = 1;
}
