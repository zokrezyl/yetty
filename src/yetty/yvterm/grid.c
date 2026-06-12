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
 *
 * This TU deliberately does NOT include its own generated header
 * `yetty/yvterm/grid.h` — that header is a downstream artifact for other
 * modules. The foundational types it needs (yclass identity, Result,
 * rectangle) are pulled in directly, and this TU declares its own
 * yetty_yvterm_grid_ptr_result below (the same one grid.h publishes for
 * consumers). The figure base type comes from the parent header figure.h.
 */
#include <stdlib.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterminal/terminal.h>
#include <yetty/ytrace/ytrace.h>

/* Absolute lowest stacking order: the container sorts children by
 * (z, insertion-seq) and renders back-to-front, so the most-negative z renders
 * first (bottom). The terminal content is the floor of the pane — it must sit
 * below EVERY other figure, including a wire app's opaque chrome backdrop
 * (ychrome host uses z=-1000000 to sit "far below every app figure"). If the
 * grid shared that z, ordering would fall to insertion-seq, and a backdrop that
 * arrives/re-mints in the wrong order (or, over a slow guest transport, arrives
 * late) would no longer reliably cover the console text. Pin the grid strictly
 * beneath the backdrop so it can never sort above the content. */
#define YETTY_YVTERM_GRID_Z (-2000000000)

struct [[clang::annotate("class@yvterm:grid")]] [[clang::annotate("parent@yfigure:figure")]]
yetty_yvterm_grid {
    /* The text+ydraw content composer this grid owns and renders. */
    struct yetty_yrender_terminal_layer *content;
};

/* Result wrapper for the grid handle. Declared here (not pulled from grid.h,
 * which this TU does not include) so the appended grid.gen.c — which defines
 * yetty_yvterm_grid_from() returning it — has the type in scope. The public
 * grid.h publishes the identical declaration for other modules. */
YETTY_YRESULT_DECLARE(yetty_yvterm_grid_ptr, struct yetty_yvterm_grid *);

/* Defined in the appended grid.gen.c (foot of this TU). Forward-declared here
 * because this TU does not include its own generated header — the class
 * accessor and the obj→body downcast are used by the helpers and the
 * object-keyed public API below. */
struct yetty_yclass_ptr_result yetty_yvterm_grid_class_get(void);
struct yetty_yvterm_grid_ptr_result yetty_yvterm_grid_from(struct yetty_yclass_object *obj);

/* ===========================================================================
 * Figure ops — slots take the yclass object; the body slice sits inside it,
 * reached via the generated downcast yetty_yvterm_grid_from.
 * ========================================================================= */

[[clang::annotate("override@yvterm:grid:yfigure:render")]]
static struct yetty_ycore_void_result grid_figure_render_slot(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              struct yetty_ydraw_target *target)
{
    (void)ctx;
    struct yetty_yvterm_grid_ptr_result grid_res = yetty_yvterm_grid_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_res, "grid_figure_render: from_obj");
    struct yetty_yvterm_grid *grid = grid_res.value;

    if (!grid->content || !grid->content->ops || !grid->content->ops->render) {
        return YETTY_OK_VOID();
    }

    /* Content insets: a client reserved a band of the pane (a docked status
     * bar / HUD) via YETTY_OSC_CS_CONTENT_INSET. The terminal already shrank
     * the grid rows/cols to the inset content rect; here we narrow the render
     * viewport (and the scissor clip) to the same rect so the text grid lands
     * at the inset origin and any tall ydraw figure clips at the content edge
     * instead of bleeding into the reserved band. render_layer maps the grid
     * into target->viewport (SetViewport + SetScissorRect), and figure passes
     * intersect their scissor with target->clip — set both. */
    struct yetty_yrender_viewport saved_viewport = target->viewport;
    struct yetty_yrender_viewport saved_clip = target->clip;
    float inset_left = grid->content->content_inset_left;
    float inset_right = grid->content->content_inset_right;
    float inset_top = grid->content->content_inset_top;
    float inset_bottom = grid->content->content_inset_bottom;
    int inset_active =
        (inset_left > 0.0f || inset_right > 0.0f || inset_top > 0.0f || inset_bottom > 0.0f);
    if (inset_active) {
        struct yetty_yrender_viewport content = {
            .x = saved_viewport.x + inset_left,
            .y = saved_viewport.y + inset_top,
            .w = saved_viewport.w - inset_left - inset_right,
            .h = saved_viewport.h - inset_top - inset_bottom,
        };
        if (content.w < 1.0f) {
            content.w = 1.0f;
        }
        if (content.h < 1.0f) {
            content.h = 1.0f;
        }
        target->viewport = content;
        target->clip = content;
    }

    /* Bottom-most child: force a full repaint so figures composited above it
     * (every pass uses LoadOp_Load) always sit on freshly-drawn content. */
    struct yetty_ycore_int_result render_res = grid->content->ops->render(grid->content, target, 1);

    if (inset_active) {
        target->viewport = saved_viewport;
        target->clip = saved_clip;
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "grid_figure_render: content render");
    return YETTY_OK_VOID();
}

[[clang::annotate("override@yvterm:grid:yfigure:destroy")]]
static struct yetty_ycore_void_result grid_figure_destroy_slot(struct yetty_yclass_ctx *ctx,
                                                               struct yetty_yclass_object *obj)
{
    (void)ctx;
    struct yetty_yvterm_grid_ptr_result grid_res = yetty_yvterm_grid_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_res, "grid_figure_destroy: from_obj");
    struct yetty_yvterm_grid *grid = grid_res.value;

    /* The content layer is BORROWED, not owned: the terminal created it and
     * destroys it directly (terminal->layer). We only drop our reference so a
     * stray render after teardown can't touch freed memory. The object itself
     * is ours to release — every concrete figure kind frees via object_free
     * in its destroy slot. */
    grid->content = NULL;
    return yetty_yclass_object_free(obj);
}

/* ===========================================================================
 * Public API
 * ========================================================================= */

/* Wrap an already-created content layer as the grid figure. Returns the yclass
 * object handle; callers route it through yetty_yvterm_grid_as_figure for
 * add_child / render and yetty_yvterm_grid_content / _from for body access. */
struct yetty_yclass_object_ptr_result yetty_yvterm_grid_figure_create(
    struct yetty_yrender_terminal_layer *content, struct yetty_ycore_rectangle rect)
{
    if (!content) {
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yvterm_grid_figure_create: content is NULL");
    }

    /* Allocate as a yclass object so the figure carries a class header
     * (enables yclass dispatch). The typed body and the embedded figure base
     * slice live inside the object; reach them via the generated downcasts. */
    struct yetty_yclass_ptr_result class_res = yetty_yvterm_grid_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res,
                        "yetty_yvterm_grid_figure_create: class");

    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res,
                        "yetty_yvterm_grid_figure_create: object_alloc");
    struct yetty_yclass_object *obj = object_res.value;

    struct yetty_yvterm_grid_ptr_result grid_res = yetty_yvterm_grid_from(obj);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, grid_res,
                        "yetty_yvterm_grid_figure_create: from_obj");
    struct yetty_yvterm_grid *grid = grid_res.value;

    grid->content = content;

    {
        struct yetty_ycore_void_result rect_set_res = yetty_yfigure_figure_rect_set(obj, rect);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, rect_set_res,
                            "yetty_yvterm_grid_figure_create: rect_set");
    }
    {
        struct yetty_ycore_void_result z_set_res =
            yetty_yfigure_figure_z_set(obj, YETTY_YVTERM_GRID_Z);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, z_set_res,
                            "yetty_yvterm_grid_figure_create: z_set");
    }
    /* Start dirty so the first container render walk paints us. */
    {
        struct yetty_ycore_void_result dirty_set_res = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dirty_set_res,
                            "yetty_yvterm_grid_figure_create: dirty_set");
    }

    ydebug("yvterm_grid: created (content=%p z=%d)", (void *)content, YETTY_YVTERM_GRID_Z);
    return YETTY_OK(yetty_yclass_object_ptr, obj);
}

/* Upcast to the figure base. The grid's figure base is the first slice in the
 * object, so it is simply (obj + 1). NULL obj yields NULL. */
struct yetty_yfigure_figure *yetty_yvterm_grid_as_figure(struct yetty_yclass_object *obj)
{
    return obj ? yetty_yfigure_figure_from(obj).value : NULL;
}

/* Borrow the owned content layer (for the terminal's direct scroll / selection
 * / anchor / resize calls during the transition). */
struct yetty_yrender_terminal_layer *yetty_yvterm_grid_content(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct yetty_yvterm_grid_ptr_result grid_res = yetty_yvterm_grid_from(obj);
    if (YETTY_IS_ERR(grid_res)) {
        yetty_ycore_error_destroy(grid_res.error);
        return NULL;
    }
    return grid_res.value ? grid_res.value->content : NULL;
}

#include "grid.gen.c"
