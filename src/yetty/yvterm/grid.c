/*
 * grid.c — the terminal content (libvterm text grid + ydraw rich-content
 * canvas) as a yfigure_figure subclass: yvterm:grid.
 *
 * This is Stage 1 of retiring the old "layer" concept. The content is seated
 * as the lowest-z child of the terminal's root container and rendered through
 * the same figure path as every other figure, instead of a bespoke pass run
 * before the container. The grid borrows the content layer handed to _create
 * (the terminal owns and destroys it) and drives its two-pass (text grid +
 * ydraw) render.
 *
 * Ownership of the libvterm screen and the per-line artifact storage (today
 * split between text-layer and ydraw's scrolling-grid) folds into this class
 * in a later step; for now it composes the existing content layer unchanged.
 */
#include <stdlib.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterminal/terminal.h>
#include <yetty/yvterm/content-layer.h>
#include <yetty/yvterm/grid.h>
#include <yetty/ytrace/ytrace.h>

/* Lowest stacking order: the container sorts children by (z, insertion-seq)
 * and renders back-to-front, so the most-negative z renders first (bottom).
 * Every other figure defaults to z=0, so this keeps the content underneath. */
#define YETTY_YVTERM_GRID_Z (-1000000)

struct [[clang::annotate("class@yvterm:grid")]] [[clang::annotate("parent@yfigure:figure")]]
yetty_yvterm_grid {
    /* Upward handle to the embedded figure base (lives at obj + 1; base is
     * its first member). */
    struct yetty_yfigure_figure *base;

    /* The text+ydraw content composer this grid owns and renders. */
    struct yetty_yrender_terminal_layer *content;
};

/* This kind's own data slice (its fields sit after the figure base slice in
 * the shared yclass object). */
static struct yetty_yclass_void_ptr_result grid_from_obj(struct yetty_yclass_object *obj)
{
    struct yetty_yclass_ptr_result class_res = yetty_yvterm_grid_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_void_ptr, class_res, "grid_from_obj: class");
    return yetty_yclass_object_data(obj, class_res.value);
}

/* ===========================================================================
 * Figure ops
 * ========================================================================= */

static struct yetty_ycore_void_result grid_figure_render(struct yetty_yfigure_figure *self,
                                                         struct yetty_ydraw_target *target)
{
    struct yetty_yclass_void_ptr_result figure_res =
        grid_from_obj((struct yetty_yclass_object *)self - 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, figure_res, "grid_figure_render: from_obj");
    struct yetty_yvterm_grid *grid = figure_res.value;

    if (!grid->content || !grid->content->ops || !grid->content->ops->render) {
        return YETTY_OK_VOID();
    }
    /* Bottom-most child: force a full repaint so figures composited above it
     * (every pass uses LoadOp_Load) always sit on freshly-drawn content. */
    struct yetty_ycore_int_result render_res = grid->content->ops->render(grid->content, target, 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "grid_figure_render: content render");
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result grid_figure_destroy(struct yetty_yfigure_figure *self)
{
    struct yetty_yclass_void_ptr_result figure_res =
        grid_from_obj((struct yetty_yclass_object *)self - 1);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, figure_res, "grid_figure_destroy: from_obj");
    struct yetty_yvterm_grid *grid = figure_res.value;

    /* The content layer is BORROWED, not owned: the terminal created it and
     * destroys it directly (terminal->layer). We only drop our reference so a
     * stray render after teardown can't touch freed memory. */
    grid->content = NULL;
    return YETTY_OK_VOID();
}

/* ===========================================================================
 * Ops vtable — cross-domain overrides of yfigure methods. Body sits at obj+1.
 * ========================================================================= */

[[clang::annotate("override@yvterm:grid:yfigure:render")]]
static struct yetty_ycore_void_result grid_figure_render_slot(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              struct yetty_ydraw_target *target)
{
    (void)ctx;
    return grid_figure_render((struct yetty_yfigure_figure *)(obj + 1), target);
}

[[clang::annotate("override@yvterm:grid:yfigure:destroy")]]
static struct yetty_ycore_void_result grid_figure_destroy_slot(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj)
{
    (void)ctx;
    return grid_figure_destroy((struct yetty_yfigure_figure *)(obj + 1));
}

/* ===========================================================================
 * Public API
 * ========================================================================= */

struct yetty_yvterm_grid_ptr_result yetty_yvterm_grid_figure_create(
    struct yetty_yrender_terminal_layer *content, struct yetty_ycore_rectangle rect)
{
    if (!content) {
        return YETTY_ERR(yetty_yvterm_grid_ptr, "yetty_yvterm_grid_figure_create: content is NULL");
    }

    /* Allocate as a yclass object so the figure carries a class header
     * (enables yclass dispatch). Typed body lives at obj + 1; the embedded
     * `base` is its first member. */
    struct yetty_yclass_ptr_result class_res = yetty_yvterm_grid_class_get();
    YETTY_RETURN_IF_ERR(yetty_yvterm_grid_ptr, class_res, "yetty_yvterm_grid_figure_create: class");

    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yvterm_grid_ptr, object_res,
                        "yetty_yvterm_grid_figure_create: object_alloc");

    struct yetty_yclass_void_ptr_result figure_res = grid_from_obj(object_res.value);
    YETTY_RETURN_IF_ERR(yetty_yvterm_grid_ptr, figure_res,
                        "yetty_yvterm_grid_figure_create: from_obj");
    struct yetty_yvterm_grid *grid = figure_res.value;

    grid->base = (struct yetty_yfigure_figure *)(object_res.value + 1);
    grid->content = content;

    {
        struct yetty_ycore_void_result rect_set_res =
            yetty_yfigure_figure_rect_set((struct yetty_yclass_object *)(grid->base) - 1, rect);
        YETTY_RETURN_IF_ERR(yetty_yvterm_grid_ptr, rect_set_res,
                            "yetty_yvterm_grid_figure_create: rect_set");
    }
    {
        struct yetty_ycore_void_result z_set_res = yetty_yfigure_figure_z_set(
            (struct yetty_yclass_object *)(grid->base) - 1, YETTY_YVTERM_GRID_Z);
        YETTY_RETURN_IF_ERR(yetty_yvterm_grid_ptr, z_set_res,
                            "yetty_yvterm_grid_figure_create: z_set");
    }
    /* Start dirty so the first container render walk paints us. */
    {
        struct yetty_ycore_void_result dirty_set_res =
            yetty_yfigure_figure_dirty_set((struct yetty_yclass_object *)(grid->base) - 1, 1);
        YETTY_RETURN_IF_ERR(yetty_yvterm_grid_ptr, dirty_set_res,
                            "yetty_yvterm_grid_figure_create: dirty_set");
    }

    ydebug("yvterm_grid: created (content=%p z=%d)", (void *)content, YETTY_YVTERM_GRID_Z);
    return YETTY_OK(yetty_yvterm_grid_ptr, grid);
}

struct yetty_yfigure_figure *yetty_yvterm_grid_as_figure(struct yetty_yvterm_grid *grid)
{
    return grid ? grid->base : NULL;
}

struct yetty_yrender_terminal_layer *yetty_yvterm_grid_content(struct yetty_yvterm_grid *grid)
{
    return grid ? grid->content : NULL;
}

#include "grid.gen.c"
