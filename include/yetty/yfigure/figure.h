/*
 * yfigure — base type for compositor figures.
 *
 * A figure is a positioned, axis-aligned rectangular thing that knows
 * how to paint itself inside its rectangle. The compositor (ycompositor,
 * defined elsewhere) hosts a list of figures and drives damage-rect
 * repaint; the figure itself is unaware of the compositor.
 *
 * Concrete figure kinds (ygrid, yimage, yplot, …) each live in their
 * own module, embed `struct yetty_yfigure_figure` as the first member,
 * and install an ops vtable. A group is itself a figure (composite
 * pattern, same model as scene-canvas).
 *
 * Coordinate system:
 *   A figure's `rect` is its position + size in absolute target pixel
 *   space. The figure knows where it is from its own state — render
 *   ops do NOT take position parameters.
 *
 *   The wire format encodes coordinates relative to the enclosing
 *   group for compactness and cheap subtree moves; the decoder
 *   translates wire-relative to runtime-absolute as it walks each
 *   CMD_GROUP. When a group moves at runtime, the move walks
 *   descendants and updates their absolute rects accordingly.
 */
#ifndef YETTY_YFIGURE_FIGURE_H
#define YETTY_YFIGURE_FIGURE_H

#include <stddef.h>

#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct yetty_yfigure_figure;
struct yetty_ydraw_target;

struct yetty_yfigure_figure_ops {
    /* Destroy concrete state and free `self`. For composite figures
     * (groups) this cascades to children. The base struct guarantees
     * `self` is non-NULL when ops are invoked. */
    struct yetty_ycore_void_result (*destroy)(struct yetty_yfigure_figure *self);

    /* Paint into `target`. The figure already knows its position and
     * size from `self->rect` (absolute, target pixel space). It uses
     * `target` polymorphically — view via `target->ops->get_view`,
     * pane via `target->viewport`. The figure owns its own pipeline
     * + binder (yplot-pattern); the concrete kind of target sitting
     * behind the handle doesn't matter to it.
     *
     * The compositor only calls render when self->rect intersects the
     * current damage region; a called figure redraws itself fully. */
    struct yetty_ycore_void_result (*render)(
        struct yetty_yfigure_figure *self,
        struct yetty_ydraw_target *target);
};

struct yetty_yfigure_figure {
    const struct yetty_yfigure_figure_ops *ops;
    /* AABB in target pixel space. Set at construction by the concrete
     * figure; subsequent moves go through the compositor's set_rect so
     * damage tracking stays correct. */
    struct yetty_ycore_rectangle rect;
    /* Set by the figure when its contents change without geometry
     * moving. The compositor ORs this into its damage region during
     * the next render pass and clears it after. */
    int dirty;
};

/*===========================================================================
 * Group — a figure that contains other figures.
 *
 * The group is itself a figure: render iterates children in insertion
 * order (back-to-front), destroy cascades.
 *
 * Children's rects, like every figure's, are in absolute target pixel
 * space. The group is primarily a lifecycle / identity container —
 * destroying a group cascades to its children. The wire decoder is
 * what translates wire-relative coords into the absolute runtime rects
 * as it walks each CMD_GROUP; once decoded, every figure knows its
 * own absolute position. A runtime "move group" helper (when needed)
 * walks descendants to translate their rects.
 *=========================================================================*/

struct yetty_yfigure_group;

YETTY_YRESULT_DECLARE(yetty_yfigure_group_ptr, struct yetty_yfigure_group *);

/* Create an empty group with the given AABB. */
struct yetty_yfigure_group_ptr_result yetty_yfigure_group_create(
    struct yetty_ycore_rectangle rect);

/* Upcast: a group is a figure. */
struct yetty_yfigure_figure *yetty_yfigure_group_as_figure(
    struct yetty_yfigure_group *group);

/* Append child at the top of the group's z-order. The group takes
 * ownership: child->ops->destroy runs when the group is destroyed or
 * the child is removed. */
struct yetty_ycore_void_result yetty_yfigure_group_add_child(
    struct yetty_yfigure_group *group, struct yetty_yfigure_figure *child);

/* Remove child without destroying. Caller resumes ownership. */
struct yetty_ycore_void_result yetty_yfigure_group_remove_child(
    struct yetty_yfigure_group *group, struct yetty_yfigure_figure *child);

/* Move child to the top of the group's z-order. */
struct yetty_ycore_void_result yetty_yfigure_group_raise_child(
    struct yetty_yfigure_group *group, struct yetty_yfigure_figure *child);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YFIGURE_FIGURE_H */
