#include <stdlib.h>
#include <yetty/yvterm/content-layer.h>
#include <yetty/yvterm/text-layer.h>
#include <yetty/yvterm/ydraw-content.h>
#include <yetty/yterminal/dcs-codes.h>
#include <yetty/yrender/render-target.h>
#include <yetty/ytrace/ytrace.h>

/* Content layer — embeds base as first member so it can be cast to/from a
 * terminal_layer and live in terminal->layers[]. Owns the text grid and the
 * ydraw canvas (each a terminal_layer used internally as a render handle +
 * op bag), and routes everything between them that used to round-trip through
 * the terminal. */
struct yetty_yvterm_content_layer {
    struct yetty_yrender_terminal_layer base;

    /* Owned sub-renderers. `text` is the bottom pass (libvterm grid), `ydraw`
     * the overlay (SDF prims + glyph drawables + embedded figures). */
    struct yetty_yrender_terminal_layer *text;
    struct yetty_yrender_terminal_layer *ydraw;

    /* Terminal's request-render hook, forwarded through content_request_render
     * so both sub-renderers can ask the terminal for a frame. */
    yetty_yterminal_request_render_fn term_request_render_fn;
    void *term_request_render_userdata;

    /* YGRID_USE_NEW_OSC=1 — captured at create. When set, the render pipeline
     * skips the ydraw pass (the shader-glyph + root container carry the
     * new-OSC stack) and renders only the text grid. */
    int new_osc_path_active;
};

/*-----------------------------------------------------------------------
 * Internal cross-wiring — replaces the terminal's broadcast callbacks.
 *---------------------------------------------------------------------*/

/* request_render passthrough — both sub-renderers call this; we forward to
 * the terminal's render request. */
static struct yetty_ycore_void_result content_request_render(void *userdata)
{
    struct yetty_yvterm_content_layer *content = userdata;
    if (content->term_request_render_fn) {
        return content->term_request_render_fn(content->term_request_render_userdata);
    }
    return YETTY_OK_VOID();
}

/* Scroll cross-propagation. When one sub-renderer scrolls (text via libvterm
 * pushline, ydraw via canvas scroll), drive the other in lockstep — with the
 * in_external_scroll guard so the driven side doesn't echo back. Mirrors the
 * old terminal_scroll_callback, now over exactly two layers. */
static struct yetty_ycore_void_result content_on_layer_scroll(
    struct yetty_yrender_terminal_layer *source, int lines, void *userdata)
{
    struct yetty_yvterm_content_layer *content = userdata;
    struct yetty_yrender_terminal_layer *layers[2] = {content->text, content->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer == source || !layer || !layer->ops || !layer->ops->scroll) {
            continue;
        }
        layer->in_external_scroll = 1;
        struct yetty_ycore_void_result res = layer->ops->scroll(layer, lines);
        layer->in_external_scroll = 0;
        YETTY_RETURN_IF_ERR(yetty_ycore_void, res, "content_on_layer_scroll: layer scroll failed");
    }
    return YETTY_OK_VOID();
}

/* Cursor cross-propagation. Mirrors the old terminal_cursor_callback. */
static struct yetty_ycore_void_result content_on_layer_cursor(
    struct yetty_yrender_terminal_layer *source, struct yetty_ycore_grid_cursor_pos cursor_pos,
    void *userdata)
{
    struct yetty_yvterm_content_layer *content = userdata;
    struct yetty_yrender_terminal_layer *layers[2] = {content->text, content->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer == source || !layer || !layer->ops || !layer->ops->set_cursor) {
            continue;
        }
        struct yetty_ycore_void_result r =
            layer->ops->set_cursor(layer, (int)cursor_pos.cols, (int)cursor_pos.rows);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "content_on_layer_cursor: layer set_cursor failed");
    }
    return YETTY_OK_VOID();
}

/* Alt-screen toggle from libvterm (fired through the text grid's alt_screen_fn).
 * libvterm owns the text-side swap; we drive the ydraw canvas's saved-state
 * swap. Mirrors the old terminal_alt_screen_callback. */
static struct yetty_ycore_void_result content_on_alt_screen(int active, void *userdata)
{
    struct yetty_yvterm_content_layer *content = userdata;
    struct yetty_yrender_terminal_layer *layers[2] = {content->text, content->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->set_alt_screen) {
            struct yetty_ycore_void_result r = layer->ops->set_alt_screen(layer, active);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "content_on_alt_screen: layer set_alt_screen failed");
        }
    }
    return content_request_render(content);
}

/* Full-screen erase from libvterm (fired through the text grid's clear_screen_fn).
 * Mirrors the old terminal_clear_screen_callback: broadcast to the sub-renderers
 * that hold orthogonal state (the ydraw canvas), no request_render here — the
 * post-feed dirty check pumps the frame. */
static struct yetty_ycore_void_result content_on_clear_screen(void *userdata)
{
    struct yetty_yvterm_content_layer *content = userdata;
    struct yetty_yrender_terminal_layer *layers[2] = {content->text, content->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->clear_screen) {
            struct yetty_ycore_void_result r = layer->ops->clear_screen(layer);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "content_on_clear_screen: layer clear_screen failed");
        }
    }
    return YETTY_OK_VOID();
}

/*-----------------------------------------------------------------------
 * terminal_layer_ops — the terminal sees exactly one layer.
 *---------------------------------------------------------------------*/

static struct yetty_ycore_void_result content_layer_destroy(struct yetty_yrender_terminal_layer *self)
{
    struct yetty_yvterm_content_layer *content =
        container_of(self, struct yetty_yvterm_content_layer, base);
    struct yetty_ycore_void_result first_err = YETTY_OK_VOID();

    /* Destroy the text grid first (matches the prior layers[] teardown order):
     * it owns the cell_ref_table the ydraw canvas borrows a cell_source into,
     * but the canvas never touches that table at destroy (handles are GC-only),
     * so the order is safe. */
    if (content->text && content->text->ops && content->text->ops->destroy) {
        struct yetty_ycore_void_result r = content->text->ops->destroy(content->text);
        if (YETTY_IS_ERR(r)) {
            first_err = r;
        }
    }
    if (content->ydraw && content->ydraw->ops && content->ydraw->ops->destroy) {
        struct yetty_ycore_void_result r = content->ydraw->ops->destroy(content->ydraw);
        if (YETTY_IS_ERR(r)) {
            if (YETTY_IS_OK(first_err)) {
                first_err = r;
            } else {
                yetty_ycore_error_destroy(r.error);
            }
        }
    }
    free(content);

    if (YETTY_IS_ERR(first_err)) {
        return YETTY_ERR(yetty_ycore_void, "content_layer_destroy: sub-renderer destroy failed",
                         first_err);
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result content_layer_resize_grid(
    struct yetty_yrender_terminal_layer *self, struct yetty_ycore_grid_size grid_size,
    struct yetty_ycore_pixel_size cell_size)
{
    struct yetty_yvterm_content_layer *content =
        container_of(self, struct yetty_yvterm_content_layer, base);
    struct yetty_yrender_terminal_layer *layers[2] = {content->text, content->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->resize_grid) {
            struct yetty_ycore_void_result r = layer->ops->resize_grid(layer, grid_size, cell_size);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "content_layer_resize_grid: layer resize_grid failed");
        }
    }
    self->grid_size = grid_size;
    self->cell_size = cell_size;
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result content_layer_set_visual_zoom(
    struct yetty_yrender_terminal_layer *self, float scale, float offset_x, float offset_y)
{
    struct yetty_yvterm_content_layer *content =
        container_of(self, struct yetty_yvterm_content_layer, base);
    struct yetty_yrender_terminal_layer *layers[2] = {content->text, content->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->set_visual_zoom) {
            struct yetty_ycore_void_result r =
                layer->ops->set_visual_zoom(layer, scale, offset_x, offset_y);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "content_layer_set_visual_zoom: layer set_visual_zoom failed");
        }
    }
    return YETTY_OK_VOID();
}

/* The content layer is never passed to render_layer (its sub-renderers are),
 * so this is only a defensive fallback — hand back the text grid's set. */
static struct yetty_yrender_gpu_resource_set_result content_layer_get_gpu_resource_set(
    const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yvterm_content_layer *content =
        container_of((struct yetty_yrender_terminal_layer *)self, struct yetty_yvterm_content_layer,
                     base);
    if (content->text && content->text->ops && content->text->ops->get_gpu_resource_set) {
        return content->text->ops->get_gpu_resource_set(content->text);
    }
    return YETTY_ERR(yetty_yrender_gpu_resource_set,
                     "content_layer_get_gpu_resource_set: no text grid");
}

/* Two-pass render into the shared target:
 *   1. text grid  (text-layer.wgsl render handle)
 *   2. ydraw prims + figures (ydraw-layer.wgsl render handle) — skipped in
 *      YGRID_USE_NEW_OSC text-only mode, or when the canvas is empty
 *   3. shader-glyph figure (text-owned, rendered after ydraw to match the
 *      prior compositor timing)
 * `force` (already folded with the terminal's dirty + root-container scan)
 * cascades upward: a lower pass that draws forces the next so its pixels
 * recomposite over fresh content. */
static struct yetty_ycore_int_result content_layer_render(struct yetty_yrender_terminal_layer *self,
                                                          struct yetty_ydraw_target *target,
                                                          int force)
{
    struct yetty_yvterm_content_layer *content =
        container_of(self, struct yetty_yvterm_content_layer, base);
    int drew = 0;
    int cascade = force;

    /* 1. text grid */
    if (content->text && content->text->ops && content->text->ops->render) {
        struct yetty_ycore_int_result tr = content->text->ops->render(content->text, target, cascade);
        YETTY_RETURN_IF_ERR(yetty_ycore_int, tr, "content_layer_render: text grid render failed");
        if (tr.value == 1) {
            drew = 1;
            cascade = 1;
        }
    }

    /* 2. ydraw prims + figures */
    if (!content->new_osc_path_active && content->ydraw && content->ydraw->ops &&
        content->ydraw->ops->render) {
        int ydraw_empty = content->ydraw->ops->is_empty && content->ydraw->ops->is_empty(content->ydraw);
        if (!ydraw_empty) {
            struct yetty_ycore_int_result yr =
                content->ydraw->ops->render(content->ydraw, target, cascade);
            YETTY_RETURN_IF_ERR(yetty_ycore_int, yr, "content_layer_render: ydraw render failed");
            if (yr.value == 1) {
                drew = 1;
                cascade = 1;
            }
        }
    }

    /* 3. shader-glyph figure (owned by the text grid). */
    struct yetty_ycore_void_result fr =
        yetty_yvterm_text_layer_render_figures(content->text, target);
    YETTY_RETURN_IF_ERR(yetty_ycore_int, fr, "content_layer_render: text figures failed");

    return YETTY_OK(yetty_ycore_int, drew);
}

static int content_layer_is_dirty(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yvterm_content_layer *content =
        container_of((struct yetty_yrender_terminal_layer *)self, struct yetty_yvterm_content_layer,
                     base);
    if (content->text && content->text->ops && content->text->ops->is_dirty &&
        content->text->ops->is_dirty(content->text)) {
        return 1;
    }
    if (!content->new_osc_path_active && content->ydraw && content->ydraw->ops) {
        int ydraw_empty = content->ydraw->ops->is_empty && content->ydraw->ops->is_empty(content->ydraw);
        if (!ydraw_empty && content->ydraw->ops->is_dirty &&
            content->ydraw->ops->is_dirty(content->ydraw)) {
            return 1;
        }
    }
    return 0;
}

/* Text grid is never empty (background + cells), so the content layer never is. */
static int content_layer_is_empty(const struct yetty_yrender_terminal_layer *self)
{
    (void)self;
    return 0;
}

static int content_layer_on_key(struct yetty_yrender_terminal_layer *self, int key, int mods)
{
    struct yetty_yvterm_content_layer *content =
        container_of(self, struct yetty_yvterm_content_layer, base);
    if (content->text && content->text->ops && content->text->ops->on_key) {
        return content->text->ops->on_key(content->text, key, mods);
    }
    return 0;
}

static int content_layer_on_char(struct yetty_yrender_terminal_layer *self, uint32_t codepoint,
                                 int mods)
{
    struct yetty_yvterm_content_layer *content =
        container_of(self, struct yetty_yvterm_content_layer, base);
    if (content->text && content->text->ops && content->text->ops->on_char) {
        return content->text->ops->on_char(content->text, codepoint, mods);
    }
    return 0;
}

/* Live anchor — the max across sub-renderers, matching the old
 * terminal_live_anchor (text and ydraw share the anchor by design; max is a
 * safe fallback if they ever drift). */
static uint32_t content_layer_get_live_anchor(const struct yetty_yrender_terminal_layer *self)
{
    const struct yetty_yvterm_content_layer *content =
        container_of((struct yetty_yrender_terminal_layer *)self, struct yetty_yvterm_content_layer,
                     base);
    uint32_t anchor = 0;
    struct yetty_yrender_terminal_layer *layers[2] = {content->text, content->ydraw};
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

static struct yetty_ycore_void_result content_layer_set_view_top(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t view_top_total_idx)
{
    struct yetty_yvterm_content_layer *content =
        container_of(self, struct yetty_yvterm_content_layer, base);
    struct yetty_yrender_terminal_layer *layers[2] = {content->text, content->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->set_view_top) {
            struct yetty_ycore_void_result r =
                layer->ops->set_view_top(layer, active, view_top_total_idx);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "content_layer_set_view_top: layer set_view_top failed");
        }
    }
    return YETTY_OK_VOID();
}

static struct yetty_ycore_void_result content_layer_set_selection(
    struct yetty_yrender_terminal_layer *self, int active, uint32_t anchor_row, uint32_t anchor_col,
    uint32_t head_row, uint32_t head_col)
{
    struct yetty_yvterm_content_layer *content =
        container_of(self, struct yetty_yvterm_content_layer, base);
    struct yetty_yrender_terminal_layer *layers[2] = {content->text, content->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->set_selection) {
            struct yetty_ycore_void_result r = layer->ops->set_selection(
                layer, active, anchor_row, anchor_col, head_row, head_col);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "content_layer_set_selection: layer set_selection failed");
        }
    }
    return YETTY_OK_VOID();
}

/* Concatenate each sub-renderer's selection-text in order (text grid first,
 * then ydraw glyphs) — matches the old terminal_collect_selection_text. */
static struct yetty_ycore_void_result content_layer_get_selection_text(
    const struct yetty_yrender_terminal_layer *self, struct yetty_ycore_buffer *out)
{
    const struct yetty_yvterm_content_layer *content =
        container_of((struct yetty_yrender_terminal_layer *)self, struct yetty_yvterm_content_layer,
                     base);
    struct yetty_yrender_terminal_layer *layers[2] = {content->text, content->ydraw};
    for (size_t i = 0; i < 2; i++) {
        struct yetty_yrender_terminal_layer *layer = layers[i];
        if (layer && layer->ops && layer->ops->get_selection_text) {
            struct yetty_ycore_void_result r = layer->ops->get_selection_text(layer, out);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, r,
                                "content_layer_get_selection_text: layer get_selection_text failed");
        }
    }
    return YETTY_OK_VOID();
}

static const struct yetty_yterminal_layer_ops content_layer_ops = {
    .destroy = content_layer_destroy,
    .resize_grid = content_layer_resize_grid,
    .set_visual_zoom = content_layer_set_visual_zoom,
    .get_gpu_resource_set = content_layer_get_gpu_resource_set,
    .render = content_layer_render,
    .is_dirty = content_layer_is_dirty,
    .is_empty = content_layer_is_empty,
    .on_key = content_layer_on_key,
    .on_char = content_layer_on_char,
    /* scroll / set_cursor are unused with a single layer — the text<->ydraw
     * cross-wiring above handles all propagation internally. */
    .get_live_anchor = content_layer_get_live_anchor,
    .set_view_top = content_layer_set_view_top,
    .set_selection = content_layer_set_selection,
    .get_selection_text = content_layer_get_selection_text,
};

/*-----------------------------------------------------------------------
 * Create / wire registration.
 *---------------------------------------------------------------------*/

struct yetty_yterminal_layer_result yetty_yvterm_content_layer_create(
    uint32_t cols, uint32_t rows, const struct yetty_context *context,
    yetty_yterminal_pty_write_fn pty_write_fn, void *pty_write_userdata,
    yetty_yterminal_request_render_fn request_render_fn, void *request_render_userdata,
    yetty_yterminal_mouse_sub_fn mouse_sub_fn, void *mouse_sub_userdata)
{
    struct yetty_yvterm_content_layer *content =
        calloc(1, sizeof(struct yetty_yvterm_content_layer));
    if (!content) {
        return YETTY_ERR(yetty_yterminal_layer, "content_layer_create: alloc failed");
    }
    content->base.ops = &content_layer_ops;
    content->term_request_render_fn = request_render_fn;
    content->term_request_render_userdata = request_render_userdata;

    {
        const char *env = getenv("YGRID_USE_NEW_OSC");
        content->new_osc_path_active = (env && env[0] == '1') ? 1 : 0;
        if (content->new_osc_path_active) {
            ydebug("content_layer_create: YGRID_USE_NEW_OSC=1 — ydraw pass skipped");
        }
    }

    /* Text grid. request_render / scroll / cursor route back into the content
     * layer so propagation stays internal. */
    struct yetty_yterminal_layer_result text_res = yetty_yvterm_text_layer_create(
        cols, rows, context, pty_write_fn, pty_write_userdata, content_request_render, content,
        content_on_layer_scroll, content, content_on_layer_cursor, content);
    if (YETTY_IS_ERR(text_res)) {
        free(content);
        return YETTY_ERR(yetty_yterminal_layer, "content_layer_create: text grid create failed",
                         text_res);
    }
    content->text = text_res.value;

    /* libvterm-fired hooks: mouse-subscription still targets the terminal (it
     * mutates terminal-side state); alt-screen / clear now drive the ydraw
     * canvas from here instead of via terminal broadcasts. */
    content->text->mouse_sub_fn = mouse_sub_fn;
    content->text->mouse_sub_userdata = mouse_sub_userdata;
    content->text->alt_screen_fn = content_on_alt_screen;
    content->text->alt_screen_userdata = content;
    content->text->clear_screen_fn = content_on_clear_screen;
    content->text->clear_screen_userdata = content;

    /* Mirror the text grid's cell/grid metrics onto the content base so the
     * terminal can read them for the initial PTY resize + root-container rect. */
    content->base.grid_size = content->text->grid_size;
    content->base.cell_size = content->text->cell_size;

    /* ydraw canvas overlay, sized off the text grid's cell metrics. */
    struct yetty_yterminal_layer_result ydraw_res = yetty_yvterm_ydraw_content_create(
        YETTY_YVTERM_YDRAW_CONTENT_KIND_SCROLLING, cols, rows, content->text->cell_size.width,
        content->text->cell_size.height, context, content_request_render, content,
        content_on_layer_scroll, content, content_on_layer_cursor, content);
    if (YETTY_IS_ERR(ydraw_res)) {
        struct yetty_ycore_void_result dr = content->text->ops->destroy(content->text);
        if (YETTY_IS_ERR(dr)) {
            yetty_ycore_error_destroy(dr.error);
        }
        free(content);
        return YETTY_ERR(yetty_yterminal_layer, "content_layer_create: ydraw create failed",
                         ydraw_res);
    }
    content->ydraw = ydraw_res.value;

    /* Bind the text grid's per-cell rich-handle table into the ydraw canvas so
     * per-cell drawable refs ride the libvterm cells. */
    {
        struct yetty_ycore_void_result br =
            yetty_yvterm_text_layer_bind_ydraw(content->text, content->ydraw);
        if (YETTY_IS_ERR(br)) {
            struct yetty_ycore_void_result dr;
            dr = content->ydraw->ops->destroy(content->ydraw);
            if (YETTY_IS_ERR(dr)) {
                yetty_ycore_error_destroy(dr.error);
            }
            dr = content->text->ops->destroy(content->text);
            if (YETTY_IS_ERR(dr)) {
                yetty_ycore_error_destroy(dr.error);
            }
            free(content);
            return YETTY_ERR(yetty_yterminal_layer, "content_layer_create: bind_ydraw failed",
                             br);
        }
    }

    ydebug("content_layer_create: %ux%u grid, cell %.1fx%.1f", cols, rows,
           content->base.cell_size.width, content->base.cell_size.height);
    return YETTY_OK(yetty_yterminal_layer, &content->base);
}

struct yetty_ycore_void_result yetty_yvterm_content_layer_register_wire(
    struct yetty_yrender_terminal_layer *self, struct yetty_ywire_wire_statemachine *sm)
{
    struct yetty_yvterm_content_layer *content =
        container_of(self, struct yetty_yvterm_content_layer, base);

    /* Text grid is the default (raw-passthrough) sink. */
    struct yetty_ycore_void_result rr = yetty_ywire_wire_statemachine_set_default(
        sm, yetty_yvterm_text_layer_process_input, content->text);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, rr,
                        "content_layer_register_wire: set_default(text grid) failed");

    /* ydraw canvas consumes the YDRAW DCS codes. */
    const int ydraw_codes[3] = {YETTY_DCS_YDRAW_CLEAR, YETTY_DCS_YDRAW_BIN, YETTY_DCS_YDRAW_OVERLAY};
    for (size_t i = 0; i < 3; i++) {
        rr = yetty_ywire_wire_statemachine_register(sm, YETTY_YWIRE_ENVELOPE_DCS, ydraw_codes[i],
                                                    /*has_args=*/1,
                                                    yetty_yvterm_ydraw_content_process_input,
                                                    content->ydraw);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, rr,
                            "content_layer_register_wire: register ydraw DCS code failed");
    }
    return YETTY_OK_VOID();
}
