/*
 * grid.c — the terminal content (libvterm text grid + ydraw rich-content
 * canvas) as a yfigure_figure subclass: yvterm:grid.
 *
 * The grid figure directly OWNS its two sub-renderers — the libvterm text grid
 * (text-layer.c) and the ydraw rich-content canvas (ydraw-content.c) — drives
 * their two render passes into the shared target, and routes everything between
 * them that used to round-trip through the terminal (scroll, cursor,
 * alt-screen, clear, selection, view-top). It is seated as the lowest-z child
 * of the terminal's root container and rendered through the same figure path as
 * every other figure. The terminal drives it through the object-keyed public
 * API in grid-api.h.
 *
 * Each sub-renderer keeps its own GPU resource set + shader, so the render slot
 * drives TWO render handles (text-layer.wgsl then ydraw-layer.wgsl) into the
 * same target — the render-target binder cache is keyed per layer pointer, so
 * two shaders need two handles. The text grid is the unit that becomes a
 * yfigure in its own right in a follow-up step, which is why it stays in its
 * own file rather than being inlined here.
 *
 * This TU deliberately does NOT include its own generated header
 * `yetty/yvterm/grid.h` — that header is a downstream artifact for other
 * modules. The foundational types it needs (yclass identity, Result,
 * rectangle) are pulled in directly, and this TU declares its own
 * yetty_yvterm_grid_ptr_result below (the same one grid.h publishes for
 * consumers). The figure base type comes from the parent header figure.h, and
 * the app-facing prototypes come from grid-api.h.
 */
#include <stdlib.h>

#include <yetty/yclass/class.h>
#include <yetty/ycore/result.h>
#include <yetty/ycore/types.h>
#include <yetty/yfigure/figure.h>
#include <yetty/yrender/render-target.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/yterminal/terminal.h>
#include <yetty/ytrace/ytrace.h>
#include <yetty/yvterm/grid-api.h>
#include <yetty/yvterm/text-layer.h>
#include <yetty/yvterm/ydraw-content.h>

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
    /* Owned sub-renderers. `text` is the bottom pass (libvterm grid), `ydraw`
     * the overlay (SDF prims + glyph drawables + embedded figures). The figure
     * destroys both in its destroy slot. */
    struct yetty_yrender_terminal_layer *text;
    struct yetty_yrender_terminal_layer *ydraw;

    /* Terminal's request-render hook, forwarded through grid_request_render so
     * both sub-renderers can ask the terminal for a frame. */
    yetty_yterminal_request_render_fn term_request_render_fn;
    void *term_request_render_userdata;

    /* YGRID_USE_NEW_OSC=1 — captured at create. When set, the render pipeline
     * skips the ydraw pass (the shader-glyph + root container carry the
     * new-OSC stack) and renders only the text grid. */
    int new_osc_path_active;

    /* Optional terminal-level hook fired after the sub-renderers are cleared on
     * a full-screen erase (CSI 2J/3J or RIS). The terminal registers it to also
     * clear its root figure container, which the sub-renderers don't own. */
    yetty_yvterm_grid_clear_hook_fn clear_hook_fn;
    void *clear_hook_userdata;

    /* Grid + cell metrics mirrored from the text grid so the terminal can read
     * them for mouse→cell mapping, the PTY resize and the root-container rect. */
    struct yetty_ycore_grid_size grid_size;
    struct yetty_ycore_pixel_size cell_size;

    /* Content insets in pane-local pixels — a client reserved a band of the
     * pane (a docked status bar / HUD) via YETTY_OSC_CS_CONTENT_INSET. The
     * render slot narrows its render viewport (and scissor clip) to
     * (pane − insets); the terminal derives the grid rows/cols from the same
     * reduced rect. All zero (the default) means the content fills the pane. */
    float content_inset_top;
    float content_inset_right;
    float content_inset_bottom;
    float content_inset_left;
};

/* Result wrapper for the grid handle. Declared here (not pulled from grid.h,
 * which this TU does not include) so the appended grid.gen.c — which defines
 * yetty_yvterm_grid_from() returning it — has the type in scope. The public
 * grid.h publishes the identical declaration for other modules. */
YETTY_YRESULT_DECLARE(yetty_yvterm_grid_ptr, struct yetty_yvterm_grid *);

/* Defined in the appended grid.gen.c (foot of this TU). Forward-declared here
 * because this TU does not include its own generated header — the class
 * accessor and the obj→body downcast are used by the slots and the
 * object-keyed public API below. */
struct yetty_yclass_ptr_result yetty_yvterm_grid_class_get(void);
struct yetty_yvterm_grid_ptr_result yetty_yvterm_grid_from(struct yetty_yclass_object *obj);

/* Resolve obj → body, absorbing the error into NULL — for the void / scalar
 * accessors where there is nothing to propagate to. */
static struct yetty_yvterm_grid *grid_body_or_null(struct yetty_yclass_object *obj)
{
    if (!obj) {
        return NULL;
    }
    struct yetty_yvterm_grid_ptr_result grid_res = yetty_yvterm_grid_from(obj);
    if (YETTY_IS_ERR(grid_res)) {
        yetty_ycore_error_destroy(grid_res.error);
        return NULL;
    }
    return grid_res.value;
}

/* ===========================================================================
 * Internal cross-wiring — drives the two sub-renderers in lockstep, replacing
 * the terminal's old broadcast callbacks. The userdata passed to the
 * sub-renderers is the grid body pointer (stable inside the yclass object).
 * ========================================================================= */

/* request_render passthrough — both sub-renderers call this; we forward to the
 * terminal's render request. */
static struct yetty_ycore_void_result grid_request_render(void *userdata)
{
    struct yetty_yvterm_grid *grid = userdata;
    if (grid->term_request_render_fn) {
        return grid->term_request_render_fn(grid->term_request_render_userdata);
    }
    return YETTY_OK_VOID();
}

/* Scroll cross-propagation. When one sub-renderer scrolls (text via libvterm
 * pushline, ydraw via canvas scroll), drive the other in lockstep — with the
 * in_external_scroll guard so the driven side doesn't echo back. */
static struct yetty_ycore_void_result grid_on_layer_scroll(
    struct yetty_yrender_terminal_layer *source, int lines, void *userdata)
{
    struct yetty_yvterm_grid *grid = userdata;
    struct yetty_yrender_terminal_layer *layers[2] = {grid->text, grid->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer == source || !layer || !layer->ops || !layer->ops->scroll) {
            continue;
        }
        layer->in_external_scroll = 1;
        struct yetty_ycore_void_result res = layer->ops->scroll(layer, lines);
        layer->in_external_scroll = 0;
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "grid_on_layer_scroll: layer scroll failed");
    }
    return YETTY_OK_VOID();
}

/* Cursor cross-propagation. */
static struct yetty_ycore_void_result grid_on_layer_cursor(
    struct yetty_yrender_terminal_layer *source, struct yetty_ycore_grid_cursor_pos cursor_pos,
    void *userdata)
{
    struct yetty_yvterm_grid *grid = userdata;
    struct yetty_yrender_terminal_layer *layers[2] = {grid->text, grid->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer == source || !layer || !layer->ops || !layer->ops->set_cursor) {
            continue;
        }
        struct yetty_ycore_void_result r =
            layer->ops->set_cursor(layer, (int)cursor_pos.cols, (int)cursor_pos.rows);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "grid_on_layer_cursor: layer set_cursor failed");
    }
    return YETTY_OK_VOID();
}

/* Alt-screen toggle from libvterm (fired through the text grid's alt_screen_fn).
 * libvterm owns the text-side swap; we drive the ydraw canvas's saved-state
 * swap. */
static struct yetty_ycore_void_result grid_on_alt_screen(int active, void *userdata)
{
    struct yetty_yvterm_grid *grid = userdata;
    struct yetty_yrender_terminal_layer *layers[2] = {grid->text, grid->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->set_alt_screen) {
            struct yetty_ycore_void_result r = layer->ops->set_alt_screen(layer, active);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "grid_on_alt_screen: layer set_alt_screen failed");
        }
    }
    return grid_request_render(grid);
}

/* Full-screen erase from libvterm (fired through the text grid's
 * clear_screen_fn): broadcast to the sub-renderers that hold orthogonal state
 * (the ydraw canvas), then fire the terminal's clear hook to wipe the root
 * figure container the sub-renderers don't own. No request_render here — the
 * post-feed dirty check pumps the frame. */
static struct yetty_ycore_void_result grid_on_clear_screen(void *userdata)
{
    struct yetty_yvterm_grid *grid = userdata;
    struct yetty_yrender_terminal_layer *layers[2] = {grid->text, grid->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->clear_screen) {
            struct yetty_ycore_void_result r = layer->ops->clear_screen(layer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "grid_on_clear_screen: layer clear_screen failed");
        }
    }
    if (grid->clear_hook_fn) {
        struct yetty_ycore_void_result r = grid->clear_hook_fn(grid->clear_hook_userdata);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "grid_on_clear_screen: clear hook failed");
    }
    return YETTY_OK_VOID();
}

/* ===========================================================================
 * Figure slots — the yclass object carries the figure base + typed body;
 * reach the body via the generated downcast yetty_yvterm_grid_from.
 * ========================================================================= */

/* Two-pass render into the shared target:
 *   1. text grid  (text-layer.wgsl render handle)
 *   2. ydraw prims + figures (ydraw-layer.wgsl render handle) — skipped in
 *      YGRID_USE_NEW_OSC text-only mode, or when the canvas is empty
 *   3. shader-glyph figure (text-owned, rendered after ydraw to match the
 *      prior compositor timing)
 * Each pass is forced (force=1): the grid is the bottom-most child and every
 * pass above uses LoadOp_Load, so its content must always be freshly drawn. */
static struct yetty_ycore_void_result grid_render_passes(struct yetty_yvterm_grid *grid,
                                                         struct yetty_ydraw_target *target)
{
    /* 1. text grid */
    if (grid->text && grid->text->ops && grid->text->ops->render) {
        struct yetty_ycore_int_result tr = grid->text->ops->render(grid->text, target, 1);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, tr, "grid_render_passes: text grid render failed");
    }

    /* 2. ydraw prims + figures */
    if (!grid->new_osc_path_active && grid->ydraw && grid->ydraw->ops && grid->ydraw->ops->render) {
        int ydraw_empty = grid->ydraw->ops->is_empty && grid->ydraw->ops->is_empty(grid->ydraw);
        if (!ydraw_empty) {
            struct yetty_ycore_int_result yr = grid->ydraw->ops->render(grid->ydraw, target, 1);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, yr, "grid_render_passes: ydraw render failed");
        }
    }

    /* 3. shader-glyph figure (owned by the text grid). */
    struct yetty_ycore_void_result fr = yetty_yvterm_text_layer_render_figures(grid->text, target);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, fr, "grid_render_passes: text figures failed");

    return YETTY_OK_VOID();
}

[[clang::annotate("override@yvterm:grid:yfigure:render")]]
static struct yetty_ycore_void_result grid_figure_render_slot(struct yetty_yclass_ctx *ctx,
                                                              struct yetty_yclass_object *obj,
                                                              struct yetty_ydraw_target *target)
{
    (void)ctx;
    struct yetty_yvterm_grid_ptr_result grid_res = yetty_yvterm_grid_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_res, "grid_figure_render: from_obj");
    struct yetty_yvterm_grid *grid = grid_res.value;

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
    int inset_active = (grid->content_inset_left > 0.0f || grid->content_inset_right > 0.0f ||
                        grid->content_inset_top > 0.0f || grid->content_inset_bottom > 0.0f);
    if (inset_active) {
        struct yetty_yrender_viewport content = {
            .x = saved_viewport.x + grid->content_inset_left,
            .y = saved_viewport.y + grid->content_inset_top,
            .w = saved_viewport.w - grid->content_inset_left - grid->content_inset_right,
            .h = saved_viewport.h - grid->content_inset_top - grid->content_inset_bottom,
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

    struct yetty_ycore_void_result render_res = grid_render_passes(grid, target);

    if (inset_active) {
        target->viewport = saved_viewport;
        target->clip = saved_clip;
    }
    YETTY_RETURN_IF_ERR(yetty_ycore_void, render_res, "grid_figure_render: render passes");
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

    /* Best-effort teardown: destroy every sub-renderer so resources are freed,
     * stash the first error, surface it at the end. The text grid goes first
     * (matches the prior teardown order): it owns the cell_ref_table the ydraw
     * canvas borrows a cell_source into, but the canvas never touches that
     * table at destroy (handles are GC-only), so the order is safe. */
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();
    if (grid->text && grid->text->ops && grid->text->ops->destroy) {
        struct yetty_ycore_void_result r = grid->text->ops->destroy(grid->text);
        if (YETTY_IS_ERR(r)) {
            first_err = r;
        }
    }
    if (grid->ydraw && grid->ydraw->ops && grid->ydraw->ops->destroy) {
        struct yetty_ycore_void_result r = grid->ydraw->ops->destroy(grid->ydraw);
        if (YETTY_IS_ERR(r)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = r;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
    }

    /* The object itself is ours to release — every concrete figure kind frees
     * via object_free in its destroy slot. */
    struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
    if (YETTY_IS_ERR(free_res)) {
        if (YETTY_IS_OK(first_err)) {
            first_err = free_res;
        } else {
            yetty_ycore_error_destroy(free_res.error);
        }
    }

    if (YETTY_IS_ERR(first_err)) {
        return YETTY_ERR(yetty_ycore_void, "grid_figure_destroy: teardown failed", first_err);
    }
    return YETTY_OK_VOID();
}

/* ===========================================================================
 * Public API — object-keyed; the terminal drives the figure through these.
 * ========================================================================= */

struct yetty_yclass_object_ptr_result yetty_yvterm_grid_figure_create(
    uint32_t cols, uint32_t rows, const struct yetty_context *context,
    yetty_yterminal_pty_write_fn pty_write_fn, void *pty_write_userdata,
    yetty_yterminal_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterminal_mouse_sub_fn mouse_sub_fn, void *mouse_sub_userdata)
{
    /* Allocate as a yclass object so the figure carries a class header (enables
     * yclass dispatch). The typed body and the embedded figure base slice live
     * inside the object; reach them via the generated downcasts. */
    struct yetty_yclass_ptr_result class_res = yetty_yvterm_grid_class_get();
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, class_res,
                        "yetty_yvterm_grid_figure_create: class");

    struct yetty_yclass_object_ptr_result object_res = yetty_yclass_object_alloc(class_res.value);
    YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, object_res,
                        "yetty_yvterm_grid_figure_create: object_alloc");
    struct yetty_yclass_object *obj = object_res.value;

    struct yetty_yvterm_grid_ptr_result grid_res = yetty_yvterm_grid_from(obj);
    if (YETTY_IS_ERR(grid_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr, "yetty_yvterm_grid_figure_create: from_obj",
                         grid_res);
    }
    struct yetty_yvterm_grid *grid = grid_res.value;

    grid->term_request_render_fn = request_render_fn;
    grid->term_request_render_userdata = request_render_userdata;

    {
        const char *env = getenv("YGRID_USE_NEW_OSC");
        grid->new_osc_path_active = (env && env[0] == '1') ? 1 : 0;
        if (grid->new_osc_path_active) {
            ydebug("yvterm_grid: YGRID_USE_NEW_OSC=1 — ydraw pass skipped");
        }
    }

    /* Text grid. request_render / scroll / cursor route back into the grid so
     * propagation stays internal. */
    struct yetty_yterminal_layer_result text_res = yetty_yvterm_text_layer_create(
        cols, rows, context, pty_write_fn, pty_write_userdata, grid_request_render, grid,
        grid_on_layer_scroll, grid, grid_on_layer_cursor, grid);
    if (YETTY_IS_ERR(text_res)) {
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yvterm_grid_figure_create: text grid create failed", text_res);
    }
    grid->text = text_res.value;

    /* libvterm-fired hooks: mouse-subscription still targets the terminal (it
     * mutates terminal-side state); alt-screen / clear drive the ydraw canvas
     * (and the terminal's clear hook) from here. */
    grid->text->mouse_sub_fn = mouse_sub_fn;
    grid->text->mouse_sub_userdata = mouse_sub_userdata;
    grid->text->alt_screen_fn = grid_on_alt_screen;
    grid->text->alt_screen_userdata = grid;
    grid->text->clear_screen_fn = grid_on_clear_screen;
    grid->text->clear_screen_userdata = grid;

    /* Mirror the text grid's cell/grid metrics so the terminal can read them. */
    grid->grid_size = grid->text->grid_size;
    grid->cell_size = grid->text->cell_size;

    /* ydraw canvas overlay, sized off the text grid's cell metrics. */
    struct yetty_yterminal_layer_result ydraw_res = yetty_yvterm_ydraw_content_create(
        YETTY_YVTERM_YDRAW_CONTENT_KIND_SCROLLING, cols, rows, grid->text->cell_size.width,
        grid->text->cell_size.height, context, grid_request_render, grid, grid_on_layer_scroll,
        grid, grid_on_layer_cursor, grid);
    if (YETTY_IS_ERR(ydraw_res)) {
        struct yetty_ycore_void_result dr = grid->text->ops->destroy(grid->text);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
        if (YETTY_IS_ERR(free_res)) {
            yetty_ycore_error_destroy(free_res.error);
        }
        return YETTY_ERR(yetty_yclass_object_ptr,
                         "yetty_yvterm_grid_figure_create: ydraw create failed", ydraw_res);
    }
    grid->ydraw = ydraw_res.value;

    /* Bind the text grid's per-cell rich-handle table into the ydraw canvas so
     * per-cell drawable refs ride the libvterm cells. */
    {
        struct yetty_ycore_void_result br =
            yetty_yvterm_text_layer_bind_ydraw(grid->text, grid->ydraw);
        if (YETTY_IS_ERR(br)) {
            struct yetty_ycore_void_result dr;
            dr = grid->ydraw->ops->destroy(grid->ydraw);
            if (YETTY_IS_ERR(dr)) {
                yetty_ycore_error_destroy(dr.error);
            }
            dr = grid->text->ops->destroy(grid->text);
            if (YETTY_IS_ERR(dr)) {
                yetty_ycore_error_destroy(dr.error);
            }
            struct yetty_ycore_void_result free_res = yetty_yclass_object_free(obj);
            if (YETTY_IS_ERR(free_res)) {
                yetty_ycore_error_destroy(free_res.error);
            }
            return YETTY_ERR(yetty_yclass_object_ptr,
                             "yetty_yvterm_grid_figure_create: bind_ydraw failed", br);
        }
    }

    /* Figure geometry: span the full grid in pane-local pixels; pin to the
     * absolute bottom; start dirty so the first container render walk paints
     * us. */
    struct yetty_ycore_rectangle rect = {
        .min = {.x = 0.0f, .y = 0.0f},
        .max = {.x = (float)cols * grid->cell_size.width,
                .y = (float)rows * grid->cell_size.height},
    };
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
    {
        struct yetty_ycore_void_result dirty_set_res = yetty_yfigure_figure_dirty_set(obj, 1);
        YETTY_RETURN_IF_ERR(yetty_yclass_object_ptr, dirty_set_res,
                            "yetty_yvterm_grid_figure_create: dirty_set");
    }

    ydebug("yvterm_grid: created (%ux%u grid, cell %.1fx%.1f, z=%d)", cols, rows,
           grid->cell_size.width, grid->cell_size.height, YETTY_YVTERM_GRID_Z);
    return YETTY_OK(yetty_yclass_object_ptr, obj);
}

/* Upcast to the figure base. The grid's figure base is the first slice in the
 * object, so it is simply (obj + 1). NULL obj yields NULL. */
struct yetty_yfigure_figure *yetty_yvterm_grid_as_figure(struct yetty_yclass_object *obj)
{
    return obj ? yetty_yfigure_figure_from(obj).value : NULL;
}

struct yetty_ycore_void_result yetty_yvterm_grid_register_wire(
    struct yetty_yclass_object *obj, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yvterm_grid_ptr_result grid_res = yetty_yvterm_grid_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_res, "yetty_yvterm_grid_register_wire: from_obj");
    struct yetty_yvterm_grid *grid = grid_res.value;

    /* Text grid is the default (raw-passthrough) sink. */
    struct yetty_ycore_void_result rr = yetty_ywire_wire_statemachine_set_default(
        sm, yetty_yvterm_text_layer_process_input, grid->text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr,
                        "yetty_yvterm_grid_register_wire: set_default(text grid) failed");

    /* ydraw canvas consumes the YDRAW DCS codes. */
    const int ydraw_codes[3] = {YETTY_DCS_YDRAW_CLEAR, YETTY_DCS_YDRAW_BIN,
                                YETTY_DCS_YDRAW_OVERLAY};
    for (size_t i = 0; i < 3; i++) {
        rr = yetty_ywire_wire_statemachine_register(
            sm, YETTY_YWIRE_ENVELOPE_DCS, ydraw_codes[i],
            /*has_args=*/1, yetty_yvterm_ydraw_content_process_input, grid->ydraw);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr,
                            "yetty_yvterm_grid_register_wire: register ydraw DCS code failed");
    }
    return YETTY_OK_VOID();
}

void yetty_yvterm_grid_set_clear_hook(struct yetty_yclass_object *obj,
                                      yetty_yvterm_grid_clear_hook_fn fn, void *userdata)
{
    struct yetty_yvterm_grid *grid = grid_body_or_null(obj);
    if (!grid) {
        return;
    }
    grid->clear_hook_fn = fn;
    grid->clear_hook_userdata = userdata;
}

struct yetty_ycore_pixel_size yetty_yvterm_grid_cell_size(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_grid *grid = grid_body_or_null(obj);
    if (!grid) {
        return (struct yetty_ycore_pixel_size){0};
    }
    return grid->cell_size;
}

void yetty_yvterm_grid_set_content_inset(struct yetty_yclass_object *obj, float top, float right,
                                         float bottom, float left)
{
    struct yetty_yvterm_grid *grid = grid_body_or_null(obj);
    if (!grid) {
        return;
    }
    grid->content_inset_top = top;
    grid->content_inset_right = right;
    grid->content_inset_bottom = bottom;
    grid->content_inset_left = left;
}

void yetty_yvterm_grid_get_content_inset(struct yetty_yclass_object *obj, float *out_top,
                                         float *out_right, float *out_bottom, float *out_left)
{
    struct yetty_yvterm_grid *grid = grid_body_or_null(obj);
    float top = grid ? grid->content_inset_top : 0.0f;
    float right = grid ? grid->content_inset_right : 0.0f;
    float bottom = grid ? grid->content_inset_bottom : 0.0f;
    float left = grid ? grid->content_inset_left : 0.0f;
    if (out_top) {
        *out_top = top;
    }
    if (out_right) {
        *out_right = right;
    }
    if (out_bottom) {
        *out_bottom = bottom;
    }
    if (out_left) {
        *out_left = left;
    }
}

struct yetty_ycore_void_result yetty_yvterm_grid_resize(struct yetty_yclass_object *obj,
                                                        struct yetty_ycore_grid_size grid_size,
                                                        struct yetty_ycore_pixel_size cell_size)
{
    struct yetty_yvterm_grid_ptr_result grid_res = yetty_yvterm_grid_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_res, "yetty_yvterm_grid_resize: from_obj");
    struct yetty_yvterm_grid *grid = grid_res.value;

    struct yetty_yrender_terminal_layer *layers[2] = {grid->text, grid->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->resize_grid) {
            struct yetty_ycore_void_result r = layer->ops->resize_grid(layer, grid_size, cell_size);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "yetty_yvterm_grid_resize: layer resize_grid failed");
        }
    }
    grid->grid_size = grid_size;
    grid->cell_size = cell_size;
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvterm_grid_set_visual_zoom(struct yetty_yclass_object *obj,
                                                                 float scale, float offset_x,
                                                                 float offset_y)
{
    struct yetty_yvterm_grid_ptr_result grid_res = yetty_yvterm_grid_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_res, "yetty_yvterm_grid_set_visual_zoom: from_obj");
    struct yetty_yvterm_grid *grid = grid_res.value;

    struct yetty_yrender_terminal_layer *layers[2] = {grid->text, grid->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->set_visual_zoom) {
            struct yetty_ycore_void_result r =
                layer->ops->set_visual_zoom(layer, scale, offset_x, offset_y);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "yetty_yvterm_grid_set_visual_zoom: layer set_visual_zoom failed");
        }
    }
    return YETTY_OK_VOID();
}

int yetty_yvterm_grid_is_dirty(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_grid *grid = grid_body_or_null(obj);
    if (!grid) {
        return 0;
    }
    if (grid->text && grid->text->ops && grid->text->ops->is_dirty &&
        grid->text->ops->is_dirty(grid->text)) {
        return 1;
    }
    if (!grid->new_osc_path_active && grid->ydraw && grid->ydraw->ops) {
        int ydraw_empty = grid->ydraw->ops->is_empty && grid->ydraw->ops->is_empty(grid->ydraw);
        if (!ydraw_empty && grid->ydraw->ops->is_dirty && grid->ydraw->ops->is_dirty(grid->ydraw)) {
            return 1;
        }
    }
    return 0;
}

int yetty_yvterm_grid_on_key(struct yetty_yclass_object *obj, int key, int mods)
{
    struct yetty_yvterm_grid *grid = grid_body_or_null(obj);
    if (grid && grid->text && grid->text->ops && grid->text->ops->on_key) {
        return grid->text->ops->on_key(grid->text, key, mods);
    }
    return 0;
}

int yetty_yvterm_grid_on_char(struct yetty_yclass_object *obj, uint32_t codepoint, int mods)
{
    struct yetty_yvterm_grid *grid = grid_body_or_null(obj);
    if (grid && grid->text && grid->text->ops && grid->text->ops->on_char) {
        return grid->text->ops->on_char(grid->text, codepoint, mods);
    }
    return 0;
}

/* Live anchor — the max across sub-renderers (text and ydraw share the anchor
 * by design; max is a safe fallback if they ever drift). */
uint32_t yetty_yvterm_grid_get_live_anchor(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_grid *grid = grid_body_or_null(obj);
    if (!grid) {
        return 0;
    }
    uint32_t anchor = 0;
    struct yetty_yrender_terminal_layer *layers[2] = {grid->text, grid->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->get_live_anchor) {
            uint32_t a = layer->ops->get_live_anchor(layer);
            if (a > anchor) {
                anchor = a;
            }
        }
    }
    return anchor;
}

/* Scrollback floor — the most-restrictive (largest) floor across sub-renderers
 * so a wheel-up never scrolls past what BOTH sub-renderers can still show. */
uint32_t yetty_yvterm_grid_get_scrollback_floor(struct yetty_yclass_object *obj)
{
    struct yetty_yvterm_grid *grid = grid_body_or_null(obj);
    if (!grid) {
        return 0;
    }
    uint32_t floor = 0;
    struct yetty_yrender_terminal_layer *layers[2] = {grid->text, grid->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->get_scrollback_floor) {
            uint32_t f = layer->ops->get_scrollback_floor(layer);
            if (f > floor) {
                floor = f;
            }
        }
    }
    return floor;
}

struct yetty_ycore_void_result yetty_yvterm_grid_set_view_top(struct yetty_yclass_object *obj,
                                                              int active,
                                                              uint32_t view_top_total_idx)
{
    struct yetty_yvterm_grid_ptr_result grid_res = yetty_yvterm_grid_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_res, "yetty_yvterm_grid_set_view_top: from_obj");
    struct yetty_yvterm_grid *grid = grid_res.value;

    struct yetty_yrender_terminal_layer *layers[2] = {grid->text, grid->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->set_view_top) {
            struct yetty_ycore_void_result r =
                layer->ops->set_view_top(layer, active, view_top_total_idx);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "yetty_yvterm_grid_set_view_top: layer set_view_top failed");
        }
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_yvterm_grid_set_selection(struct yetty_yclass_object *obj,
                                                               int active, uint32_t anchor_row,
                                                               uint32_t anchor_col,
                                                               uint32_t head_row, uint32_t head_col)
{
    struct yetty_yvterm_grid_ptr_result grid_res = yetty_yvterm_grid_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_res, "yetty_yvterm_grid_set_selection: from_obj");
    struct yetty_yvterm_grid *grid = grid_res.value;

    struct yetty_yrender_terminal_layer *layers[2] = {grid->text, grid->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->set_selection) {
            struct yetty_ycore_void_result r = layer->ops->set_selection(
                layer, active, anchor_row, anchor_col, head_row, head_col);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "yetty_yvterm_grid_set_selection: layer set_selection failed");
        }
    }
    return YETTY_OK_VOID();
}

/* Concatenate each sub-renderer's selection-text in order (text grid first,
 * then ydraw glyphs). */
struct yetty_ycore_void_result yetty_yvterm_grid_get_selection_text(struct yetty_yclass_object *obj,
                                                                    struct yetty_ycore_buffer *out)
{
    struct yetty_yvterm_grid_ptr_result grid_res = yetty_yvterm_grid_from(obj);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, grid_res,
                        "yetty_yvterm_grid_get_selection_text: from_obj");
    struct yetty_yvterm_grid *grid = grid_res.value;

    struct yetty_yrender_terminal_layer *layers[2] = {grid->text, grid->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->get_selection_text) {
            struct yetty_ycore_void_result r = layer->ops->get_selection_text(layer, out);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "yetty_yvterm_grid_get_selection_text: layer failed");
        }
    }
    return YETTY_OK_VOID();
}

#include "grid.gen.c"
