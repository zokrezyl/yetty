/*
 * ygui_popup_menu.c — POPUP_MENU widget.
 *
 * Floating, vertically-stacked list of clickable items. Inherits the
 * visuals of yetty_ygui_old_engine_popup (rounded body + drop shadow
 * + optional modal overlay) and specialises the body for menu rows: a
 * single TEXT_SPAN per row, with the hovered row highlighted using
 * theme->bg_hover. Inheritance in C is structural — we reuse the same
 * helper primitives the popup dialog draws with (render_box,
 * render_box_shadow, render_text) so the two widgets feel visually
 * identical without sharing storage.
 *
 * Items live inside the widget rather than as sub-widget rows so the
 * menu can size itself to its content (height = n_items * row_h) and
 * stay a single hit-test target. The on_press handler does the row
 * resolution + callback fan-out + close in one place.
 */

#include "ygui_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void yetty_ygui_old_engine_attach_widget(struct yetty_ygui_old_engine *engine,
                                         struct yetty_ygui_old_widget *widget);

#define MENU_DEFAULT_ITEM_H 28.0f
#define MENU_PAD_X 12.0f
#define MENU_PAD_Y 6.0f
#define MENU_SEPARATOR_H 8.0f
#define MENU_SEPARATOR_LINE 1.0f

/* Header strip (title / breadcrumb / back arrow). Same height as a row
 * so the visual rhythm stays consistent with the items below. */
#define MENU_HEADER_H 28.0f
#define MENU_HEADER_SEPARATOR_H 1.0f

/* `<` back-button hit area, anchored at the left of the header strip.
 * Width chosen so the chevron has comfortable padding without eating
 * too much of the title space. */
#define MENU_BACK_BTN_W 32.0f

static int menu_has_header(const struct yetty_ygui_old_widget *self)
{
    /* A header is rendered whenever the menu has either a title to
     * display or a back-handler (the `<` button needs space even with
     * no title — e.g. an unnamed drill-down level). */
    return (self->data.popup_menu.title && self->data.popup_menu.title[0]) ||
           self->data.popup_menu.on_back != NULL;
}

static float menu_header_h(const struct yetty_ygui_old_widget *self)
{
    return menu_has_header(self) ? (MENU_HEADER_H + MENU_HEADER_SEPARATOR_H) : 0.0f;
}

static float menu_item_h(const struct yetty_ygui_old_widget *self)
{
    if (self->data.popup_menu.item_h > 0) {
        return self->data.popup_menu.item_h;
    }
    return MENU_DEFAULT_ITEM_H;
}

/* y-offset of item `i` from the menu's top, in menu-local coords.
 * The optional header strip pushes all items down by header_h. */
static float menu_item_top(const struct yetty_ygui_old_widget *self, int i)
{
    float y = menu_header_h(self) + MENU_PAD_Y;
    float ih = menu_item_h(self);
    for (int k = 0; k < i; k++) {
        y += self->data.popup_menu.item_labels[k] ? ih : MENU_SEPARATOR_H;
    }
    return y;
}

/* Total height of all rows + vertical padding (and the header when
 * present). The widget's authored height is kept in sync with this
 * value via menu_resize() so the surrounding layout (none today —
 * menus are absolute-positioned) and the drop-shadow extent agree on
 * where the menu ends. */
static float menu_total_h(const struct yetty_ygui_old_widget *self)
{
    float h = menu_header_h(self) + 2 * MENU_PAD_Y;
    float ih = menu_item_h(self);
    for (int k = 0; k < self->data.popup_menu.n_items; k++) {
        h += self->data.popup_menu.item_labels[k] ? ih : MENU_SEPARATOR_H;
    }
    return h;
}

static void menu_resize(struct yetty_ygui_old_widget *self)
{
    float h = menu_total_h(self);
    self->authored_h = h;
    self->h = h;
    self->layout_h = h;
}

/*=============================================================================
 * Render
 *===========================================================================*/

static struct yetty_ycore_void_result popup_menu_render(struct yetty_ygui_old_widget *self,
                                                        struct yetty_ygui_old_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_OLD_FLAG_OPEN)) {
        return YETTY_OK_VOID();
    }
    const struct yetty_ygui_old_theme *theme = ctx->theme;
    float ih = menu_item_h(self);

    /* Modal overlay — fades the rest of the canvas so the user reads
     * the menu first. Drawn before the menu body so the body lands on
     * top. */
    if (self->data.popup_menu.modal && self->engine) {
        yetty_ygui_old_render_ctx_render_box(ctx, -self->x, -self->y, self->engine->width,
                                             self->engine->height, theme->overlay_modal, 0);
    }

    /* Soft drop shadow — mirrors the popup dialog. */
    yetty_ygui_old_render_ctx_render_box_shadow(ctx, self->x, self->y, self->w, self->h,
                                                theme->radius_large, theme->elevation_high,
                                                theme->shadow, theme->elevation_alpha);

    /* Body + outline. */
    yetty_ygui_old_render_ctx_render_box(
        ctx, self->x, self->y, self->w, self->h,
        theme->bg_dropdown ? theme->bg_dropdown : theme->bg_primary, theme->radius_large);
    yetty_ygui_old_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h,
                                                 theme->border_muted, theme->radius_large, 1.0f);

    float fs = theme->font_size > 0 ? theme->font_size : 14.0f;
    int hover = self->data.popup_menu.hover_index;

    /* Header strip (drill-down breadcrumb / back). Painted before the
     * item rows so they land underneath the hairline divider. */
    if (menu_has_header(self)) {
        float header_y = self->y;
        /* `<` back glyph at the left when a handler is set. The render
         * context has no rotated-box primitive — render the chevron as
         * a text glyph instead (covers any glyph the font ships). */
        float title_x = self->x + MENU_PAD_X;
        if (self->data.popup_menu.on_back) {
            float bx = self->x;
            float ty = header_y + (MENU_HEADER_H - fs) * 0.5f;
            yetty_ygui_old_render_ctx_render_text(
                ctx, "<", bx + MENU_BACK_BTN_W * 0.5f - fs * 0.25f, ty,
                theme->accent ? theme->accent : theme->text_primary, fs);
            title_x = self->x + MENU_BACK_BTN_W;
        }
        /* Title — left-aligned right of the back button (or right of
         * the padding when no back), vertically centred. text_muted so
         * the breadcrumb reads as a passive label rather than a row. */
        const char *title = self->data.popup_menu.title;
        if (title && title[0]) {
            float ty = header_y + (MENU_HEADER_H - fs) * 0.5f;
            yetty_ygui_old_render_ctx_render_text(ctx, title, title_x, ty, theme->text_muted, fs);
        }
        /* Hairline separator below the header. */
        yetty_ygui_old_render_ctx_render_box(ctx, self->x + 4.0f, self->y + MENU_HEADER_H,
                                             self->w - 8.0f, MENU_HEADER_SEPARATOR_H,
                                             theme->border_muted, 0.0f);
    }

    for (int i = 0; i < self->data.popup_menu.n_items; i++) {
        const char *label = self->data.popup_menu.item_labels[i];
        if (!label) {
            /* Separator row — short horizontal divider centred in its
             * MENU_SEPARATOR_H band. */
            float y =
                self->y + menu_item_top(self, i) + (MENU_SEPARATOR_H - MENU_SEPARATOR_LINE) * 0.5f;
            yetty_ygui_old_render_ctx_render_box(ctx, self->x + MENU_PAD_X * 0.5f, y,
                                                 self->w - MENU_PAD_X, MENU_SEPARATOR_LINE,
                                                 theme->border_muted, 0.0f);
            continue;
        }

        float row_y = self->y + menu_item_top(self, i);

        if (i == hover) {
            yetty_ygui_old_render_ctx_render_box(ctx, self->x + 4.0f, row_y, self->w - 8.0f, ih,
                                                 theme->bg_hover, theme->radius_small);
        }

        float ty = row_y + (ih - fs) * 0.5f;
        yetty_ygui_old_render_ctx_render_text(ctx, label, self->x + MENU_PAD_X, ty,
                                              theme->text_primary, fs);
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Hit-test on press → fire item callback → close.
 *===========================================================================*/

static int menu_hit_item(const struct yetty_ygui_old_widget *self, float ly)
{
    float header_h = menu_header_h(self);
    if (ly < header_h + MENU_PAD_Y || ly > self->h - MENU_PAD_Y) {
        return -1;
    }
    float y = header_h + MENU_PAD_Y;
    float ih = menu_item_h(self);
    for (int i = 0; i < self->data.popup_menu.n_items; i++) {
        float row_h = self->data.popup_menu.item_labels[i] ? ih : MENU_SEPARATOR_H;
        if (ly >= y && ly < y + row_h) {
            return self->data.popup_menu.item_labels[i] ? i : -1;
        }
        y += row_h;
    }
    return -1;
}

/* True if (lx, ly) lands on the header's back-button hit area. */
static int menu_hit_back(const struct yetty_ygui_old_widget *self, float lx, float ly)
{
    if (!self->data.popup_menu.on_back || !menu_has_header(self)) {
        return 0;
    }
    return lx >= 0.0f && lx < MENU_BACK_BTN_W && ly >= 0.0f && ly < MENU_HEADER_H;
}

static int popup_menu_on_press(struct yetty_ygui_old_widget *self, float lx, float ly,
                               ygui_event_t *out)
{
    (void)out;
    if (!(self->flags & YETTY_YGUI_OLD_FLAG_OPEN)) {
        return 0;
    }
    /* Clicks outside the menu body close it WITHOUT firing any item
     * callback. The hit area covers the menu rect; lx/ly are widget-
     * local so the in-body region is 0..w x 0..h. */
    if (lx < 0 || lx > self->w || ly < 0 || ly > self->h) {
        self->flags &= ~YETTY_YGUI_OLD_FLAG_OPEN;
        if (self->engine) {
            self->engine->dirty = 1;
            self->dirty = 1;
            yetty_ygui_old_internal_queue_delete_subtree_rendered(self);
        }
        return 1;
    }
    /* Back button — fire the handler and KEEP the menu open. The
     * handler typically calls popup_menu_clear + popup_menu_set_title +
     * popup_menu_add_item to redraw the parent level in place. */
    if (menu_hit_back(self, lx, ly)) {
        ygui_widget_click_fn back_cb = self->data.popup_menu.on_back;
        void *back_ud = self->data.popup_menu.on_back_userdata;
        if (back_cb) {
            back_cb(self, back_ud);
        }
        if (self->engine) {
            self->engine->dirty = 1;
            self->dirty = 1;
        }
        return 1;
    }
    int idx = menu_hit_item(self, ly);
    int is_drill = 0;
    if (idx >= 0 && self->data.popup_menu.item_callbacks[idx]) {
        is_drill =
            self->data.popup_menu.item_is_drill ? self->data.popup_menu.item_is_drill[idx] : 0;
        self->data.popup_menu.item_callbacks[idx](self, self->data.popup_menu.item_userdata[idx]);
    }
    /* Drill-down items keep the menu open so the callback's in-place
     * repopulation is visible. Action items (and unhandled clicks in
     * the header strip's title area) close the menu as before. */
    if (!is_drill) {
        self->flags &= ~YETTY_YGUI_OLD_FLAG_OPEN;
        if (self->engine) {
            self->engine->dirty = 1;
            self->dirty = 1;
            yetty_ygui_old_internal_queue_delete_subtree_rendered(self);
        }
    } else if (self->engine) {
        self->engine->dirty = 1;
        self->dirty = 1;
    }
    return 1;
}

static void popup_menu_destroy(struct yetty_ygui_old_widget *self)
{
    if (self->data.popup_menu.item_labels) {
        for (int i = 0; i < self->data.popup_menu.n_items; i++) {
            free(self->data.popup_menu.item_labels[i]);
        }
        free(self->data.popup_menu.item_labels);
    }
    free(self->data.popup_menu.item_callbacks);
    free(self->data.popup_menu.item_userdata);
    free(self->data.popup_menu.item_is_drill);
    free(self->data.popup_menu.title);
}

/* Custom render_all so the menu skips the engine's spatial grid when
 * it isn't OPEN. The default render_all_default sets was_rendered = 1
 * for any visible widget; with the menu's default VISIBLE flag the
 * result was a "ghost" rect parked at the last-open position, eating
 * every subsequent click on that area (tabbar close-X buttons, mainly)
 * even though nothing was painted there. Bailing out before
 * was_rendered keeps the menu out of the grid until it's reopened.
 *
 * The drawables MUST be wrapped in emit_self_in_group so the receiver
 * routes them to the menu's own scene-canvas entity (keyed by
 * group_id). Without the wrapper the prims attach to whatever entity
 * is the current parser scope (ROOT) and a later DELETE(menu_id)
 * targets nothing — the menu's body would accumulate on ROOT forever. */
static struct yetty_ycore_void_result popup_menu_render_all(struct yetty_ygui_old_widget *self,
                                                            struct yetty_ygui_old_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_OLD_FLAG_VISIBLE) || !(self->flags & YETTY_YGUI_OLD_FLAG_OPEN)) {
        return YETTY_OK_VOID();
    }
    self->was_rendered = 1;
    return yetty_ygui_old_widget_emit_self_in_group(self, ctx, popup_menu_render);
}

static const struct yetty_ygui_old_widget_vtable popup_menu_vtable = {
    .render = popup_menu_render,
    .render_all = popup_menu_render_all,
    .on_press = popup_menu_on_press,
    .destroy = popup_menu_destroy,
};

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_ygui_old_widget *yetty_ygui_old_engine_popup_menu(struct yetty_ygui_old_engine *engine,
                                                               const char *id, float x, float y,
                                                               float w)
{
    struct yetty_ygui_old_widget *m =
        yetty_ygui_old_engine_widget_alloc(engine, YETTY_YGUI_OLD_WIDGET_POPUP_MENU, id);
    if (!m) {
        return NULL;
    }
    yetty_ygui_old_widget_init_base(m, x, y, w, 2 * MENU_PAD_Y);
    m->data.popup_menu.item_labels = NULL;
    m->data.popup_menu.item_callbacks = NULL;
    m->data.popup_menu.item_userdata = NULL;
    m->data.popup_menu.item_is_drill = NULL;
    m->data.popup_menu.n_items = 0;
    m->data.popup_menu.capacity = 0;
    m->data.popup_menu.item_h = 0.0f;
    m->data.popup_menu.modal = 0;
    m->data.popup_menu.hover_index = -1;
    m->data.popup_menu.title = NULL;
    m->data.popup_menu.on_back = NULL;
    m->data.popup_menu.on_back_userdata = NULL;
    m->flags &= ~YETTY_YGUI_OLD_FLAG_OPEN; /* start closed */
    m->vtable = &popup_menu_vtable;
    /* Position is absolute — menus pop up over everything else. */
    m->layout.position = YETTY_YGUI_OLD_POSITION_ABSOLUTE;
    yetty_ygui_old_engine_attach_widget(engine, m);
    return m;
}

static int menu_grow(struct yetty_ygui_old_widget *self, int need)
{
    if (need <= self->data.popup_menu.capacity) {
        return 1;
    }
    int cap = self->data.popup_menu.capacity ? self->data.popup_menu.capacity * 2 : 8;
    while (cap < need) {
        cap *= 2;
    }
    char **labels =
        (char **)realloc(self->data.popup_menu.item_labels, (size_t)cap * sizeof(char *));
    ygui_widget_click_fn *cbs = (ygui_widget_click_fn *)realloc(
        self->data.popup_menu.item_callbacks, (size_t)cap * sizeof(ygui_widget_click_fn));
    void **udata =
        (void **)realloc(self->data.popup_menu.item_userdata, (size_t)cap * sizeof(void *));
    int *drills = (int *)realloc(self->data.popup_menu.item_is_drill, (size_t)cap * sizeof(int));
    if (!labels || !cbs || !udata || !drills) {
        /* Partial grows are fine — next call retries. Free nothing. */
        if (labels) {
            self->data.popup_menu.item_labels = labels;
        }
        if (cbs) {
            self->data.popup_menu.item_callbacks = cbs;
        }
        if (udata) {
            self->data.popup_menu.item_userdata = udata;
        }
        if (drills) {
            self->data.popup_menu.item_is_drill = drills;
        }
        return 0;
    }
    self->data.popup_menu.item_labels = labels;
    self->data.popup_menu.item_callbacks = cbs;
    self->data.popup_menu.item_userdata = udata;
    self->data.popup_menu.item_is_drill = drills;
    self->data.popup_menu.capacity = cap;
    return 1;
}

static void menu_add_row(struct yetty_ygui_old_widget *menu, const char *label,
                         ygui_click_callback_t cb, void *userdata, int is_drill)
{
    if (!menu || menu->type != YETTY_YGUI_OLD_WIDGET_POPUP_MENU) {
        return;
    }
    if (!menu_grow(menu, menu->data.popup_menu.n_items + 1)) {
        return;
    }
    int i = menu->data.popup_menu.n_items;
    menu->data.popup_menu.item_labels[i] = label ? ygui_strdup(label) : NULL;
    menu->data.popup_menu.item_callbacks[i] = cb;
    menu->data.popup_menu.item_userdata[i] = userdata;
    menu->data.popup_menu.item_is_drill[i] = is_drill;
    menu->data.popup_menu.n_items = i + 1;
    menu_resize(menu);
    if (menu->engine) {
        menu->engine->dirty = 1;
        menu->dirty = 1;
    }
}

void yetty_ygui_old_widget_popup_menu_add_item(struct yetty_ygui_old_widget *menu,
                                               const char *label, ygui_click_callback_t cb,
                                               void *userdata)
{
    menu_add_row(menu, label ? label : "", cb, userdata, /*is_drill=*/0);
}

void yetty_ygui_old_widget_popup_menu_add_drill_item(struct yetty_ygui_old_widget *menu,
                                                     const char *label, ygui_click_callback_t cb,
                                                     void *userdata)
{
    menu_add_row(menu, label ? label : "", cb, userdata, /*is_drill=*/1);
}

void yetty_ygui_old_widget_popup_menu_add_separator(struct yetty_ygui_old_widget *menu)
{
    /* Separator row: NULL label is the sentinel. is_drill is irrelevant
     * (no callback fires) but stored as 0 for cleanliness. */
    menu_add_row(menu, /*label=*/NULL, /*cb=*/NULL, /*userdata=*/NULL, /*is_drill=*/0);
}

void yetty_ygui_old_widget_popup_menu_clear(struct yetty_ygui_old_widget *menu)
{
    if (!menu || menu->type != YETTY_YGUI_OLD_WIDGET_POPUP_MENU) {
        return;
    }
    for (int i = 0; i < menu->data.popup_menu.n_items; i++) {
        free(menu->data.popup_menu.item_labels[i]);
        menu->data.popup_menu.item_labels[i] = NULL;
    }
    menu->data.popup_menu.n_items = 0;
    menu_resize(menu);
    if (menu->engine) {
        menu->engine->dirty = 1;
        menu->dirty = 1;
    }
}

void yetty_ygui_old_widget_popup_menu_set_title(struct yetty_ygui_old_widget *menu,
                                                const char *title)
{
    if (!menu || menu->type != YETTY_YGUI_OLD_WIDGET_POPUP_MENU) {
        return;
    }
    free(menu->data.popup_menu.title);
    menu->data.popup_menu.title = (title && title[0]) ? ygui_strdup(title) : NULL;
    menu_resize(menu);
    if (menu->engine) {
        menu->engine->dirty = 1;
        menu->dirty = 1;
    }
}

void yetty_ygui_old_widget_popup_menu_set_back(struct yetty_ygui_old_widget *menu,
                                               ygui_click_callback_t on_back, void *userdata)
{
    if (!menu || menu->type != YETTY_YGUI_OLD_WIDGET_POPUP_MENU) {
        return;
    }
    menu->data.popup_menu.on_back = on_back;
    menu->data.popup_menu.on_back_userdata = userdata;
    menu_resize(menu);
    if (menu->engine) {
        menu->engine->dirty = 1;
        menu->dirty = 1;
    }
}

void yetty_ygui_old_widget_popup_menu_open_at(struct yetty_ygui_old_widget *menu, float x, float y)
{
    if (!menu || menu->type != YETTY_YGUI_OLD_WIDGET_POPUP_MENU) {
        return;
    }
    menu->authored_x = x;
    menu->authored_y = y;
    menu->x = x;
    menu->y = y;
    menu->layout_x = x;
    menu->layout_y = y;
    menu->flags |= YETTY_YGUI_OLD_FLAG_OPEN;
    /* Float above everything painted earlier in the frame. */
    yetty_ygui_old_internal_bring_to_front(menu);
    if (menu->engine) {
        menu->engine->dirty = 1;
        menu->dirty = 1;
    }
}

void yetty_ygui_old_widget_popup_menu_close(struct yetty_ygui_old_widget *menu)
{
    if (!menu || menu->type != YETTY_YGUI_OLD_WIDGET_POPUP_MENU) {
        return;
    }
    menu->flags &= ~YETTY_YGUI_OLD_FLAG_OPEN;
    if (menu->engine) {
        menu->engine->dirty = 1;
        menu->dirty = 1;
        yetty_ygui_old_internal_queue_delete_subtree_rendered(menu);
    }
}

void yetty_ygui_old_widget_popup_menu_set_modal(struct yetty_ygui_old_widget *menu, int modal)
{
    if (!menu || menu->type != YETTY_YGUI_OLD_WIDGET_POPUP_MENU) {
        return;
    }
    menu->data.popup_menu.modal = modal ? 1 : 0;
}

int yetty_ygui_old_widget_popup_menu_is_open(const struct yetty_ygui_old_widget *menu)
{
    if (!menu || menu->type != YETTY_YGUI_OLD_WIDGET_POPUP_MENU) {
        return 0;
    }
    return (menu->flags & YETTY_YGUI_OLD_FLAG_OPEN) ? 1 : 0;
}
