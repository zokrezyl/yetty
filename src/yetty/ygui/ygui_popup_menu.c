/*
 * ygui_popup_menu.c — POPUP_MENU widget.
 *
 * Floating, vertically-stacked list of clickable items. Inherits the
 * visuals of yetty_ygui_engine_popup (rounded body + drop shadow
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

void yetty_ygui_engine_attach_widget(struct yetty_ygui_engine *engine,
                                     struct yetty_ygui_widget *widget);

#define MENU_DEFAULT_ITEM_H 28.0f
#define MENU_PAD_X 12.0f
#define MENU_PAD_Y 6.0f
#define MENU_SEPARATOR_H 8.0f
#define MENU_SEPARATOR_LINE 1.0f

static float menu_item_h(const struct yetty_ygui_widget *self)
{
    if (self->data.popup_menu.item_h > 0) {
        return self->data.popup_menu.item_h;
    }
    return MENU_DEFAULT_ITEM_H;
}

/* y-offset of item `i` from the menu's top, in menu-local coords. */
static float menu_item_top(const struct yetty_ygui_widget *self, int i)
{
    float y = MENU_PAD_Y;
    float ih = menu_item_h(self);
    for (int k = 0; k < i; k++) {
        y += self->data.popup_menu.item_labels[k] ? ih : MENU_SEPARATOR_H;
    }
    return y;
}

/* Total height of all rows + vertical padding. The widget's authored
 * height is kept in sync with this value via menu_resize() so the
 * surrounding layout (none today — menus are absolute-positioned) and
 * the drop-shadow extent agree on where the menu ends. */
static float menu_total_h(const struct yetty_ygui_widget *self)
{
    float h = 2 * MENU_PAD_Y;
    float ih = menu_item_h(self);
    for (int k = 0; k < self->data.popup_menu.n_items; k++) {
        h += self->data.popup_menu.item_labels[k] ? ih : MENU_SEPARATOR_H;
    }
    return h;
}

static void menu_resize(struct yetty_ygui_widget *self)
{
    float h = menu_total_h(self);
    self->authored_h = h;
    self->h = h;
    self->layout_h = h;
}

/*=============================================================================
 * Render
 *===========================================================================*/

static struct yetty_ycore_void_result popup_menu_render(struct yetty_ygui_widget *self,
                                                        struct yetty_ygui_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_FLAG_OPEN)) {
        return YETTY_OK_VOID();
    }
    const struct yetty_ygui_theme *theme = ctx->theme;
    float ih = menu_item_h(self);

    /* Modal overlay — fades the rest of the canvas so the user reads
     * the menu first. Drawn before the menu body so the body lands on
     * top. */
    if (self->data.popup_menu.modal && self->engine) {
        yetty_ygui_render_ctx_render_box(ctx, -self->x, -self->y, self->engine->width,
                                         self->engine->height, theme->overlay_modal, 0);
    }

    /* Soft drop shadow — mirrors the popup dialog. */
    yetty_ygui_render_ctx_render_box_shadow(ctx, self->x, self->y, self->w, self->h,
                                            theme->radius_large, theme->elevation_high,
                                            theme->shadow, theme->elevation_alpha);

    /* Body + outline. */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h,
                                     theme->bg_dropdown ? theme->bg_dropdown : theme->bg_primary,
                                     theme->radius_large);
    yetty_ygui_render_ctx_render_box_outline(ctx, self->x, self->y, self->w, self->h,
                                             theme->border_muted, theme->radius_large, 1.0f);

    float fs = theme->font_size > 0 ? theme->font_size : 14.0f;
    int hover = self->data.popup_menu.hover_index;

    for (int i = 0; i < self->data.popup_menu.n_items; i++) {
        const char *label = self->data.popup_menu.item_labels[i];
        if (!label) {
            /* Separator row — short horizontal divider centred in its
             * MENU_SEPARATOR_H band. */
            float y = self->y + menu_item_top(self, i) + (MENU_SEPARATOR_H - MENU_SEPARATOR_LINE) * 0.5f;
            yetty_ygui_render_ctx_render_box(ctx, self->x + MENU_PAD_X * 0.5f, y,
                                             self->w - MENU_PAD_X, MENU_SEPARATOR_LINE,
                                             theme->border_muted, 0.0f);
            continue;
        }

        float row_y = self->y + menu_item_top(self, i);

        if (i == hover) {
            yetty_ygui_render_ctx_render_box(ctx, self->x + 4.0f, row_y, self->w - 8.0f, ih,
                                             theme->bg_hover, theme->radius_small);
        }

        float ty = row_y + (ih - fs) * 0.5f;
        yetty_ygui_render_ctx_render_text(ctx, label, self->x + MENU_PAD_X, ty,
                                          theme->text_primary, fs);
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Hit-test on press → fire item callback → close.
 *===========================================================================*/

static int menu_hit_item(const struct yetty_ygui_widget *self, float ly)
{
    if (ly < MENU_PAD_Y || ly > self->h - MENU_PAD_Y) {
        return -1;
    }
    float y = MENU_PAD_Y;
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

static int popup_menu_on_press(struct yetty_ygui_widget *self, float lx, float ly,
                               ygui_event_t *out)
{
    (void)out;
    if (!(self->flags & YETTY_YGUI_FLAG_OPEN)) {
        return 0;
    }
    /* Clicks outside the menu body close it WITHOUT firing any item
     * callback. The hit area covers the menu rect; lx/ly are widget-
     * local so the in-body region is 0..w x 0..h. */
    if (lx < 0 || lx > self->w || ly < 0 || ly > self->h) {
        self->flags &= ~YETTY_YGUI_FLAG_OPEN;
        if (self->engine) {
            self->engine->dirty = 1;
        }
        return 1;
    }
    int idx = menu_hit_item(self, ly);
    if (idx >= 0 && self->data.popup_menu.item_callbacks[idx]) {
        self->data.popup_menu.item_callbacks[idx](self, self->data.popup_menu.item_userdata[idx]);
    }
    self->flags &= ~YETTY_YGUI_FLAG_OPEN;
    if (self->engine) {
        self->engine->dirty = 1;
    }
    return 1;
}

static void popup_menu_destroy(struct yetty_ygui_widget *self)
{
    if (self->data.popup_menu.item_labels) {
        for (int i = 0; i < self->data.popup_menu.n_items; i++) {
            free(self->data.popup_menu.item_labels[i]);
        }
        free(self->data.popup_menu.item_labels);
    }
    free(self->data.popup_menu.item_callbacks);
    free(self->data.popup_menu.item_userdata);
}

/* Custom render_all so the menu skips the engine's spatial grid when
 * it isn't OPEN. The default render_all_default sets was_rendered = 1
 * for any visible widget; with the menu's default VISIBLE flag the
 * result was a "ghost" rect parked at the last-open position, eating
 * every subsequent click on that area (tabbar close-X buttons, mainly)
 * even though nothing was painted there. Bailing out before
 * was_rendered keeps the menu out of the grid until it's reopened. */
static struct yetty_ycore_void_result popup_menu_render_all(struct yetty_ygui_widget *self,
                                                            struct yetty_ygui_render_ctx *ctx)
{
    if (!(self->flags & YETTY_YGUI_FLAG_VISIBLE) ||
        !(self->flags & YETTY_YGUI_FLAG_OPEN)) {
        return YETTY_OK_VOID();
    }
    self->was_rendered = 1;
    return popup_menu_render(self, ctx);
}

static const struct yetty_ygui_widget_vtable popup_menu_vtable = {
    .render = popup_menu_render,
    .render_all = popup_menu_render_all,
    .on_press = popup_menu_on_press,
    .destroy = popup_menu_destroy,
};

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_popup_menu(struct yetty_ygui_engine *engine,
                                                       const char *id, float x, float y, float w)
{
    struct yetty_ygui_widget *m =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_POPUP_MENU, id);
    if (!m) {
        return NULL;
    }
    yetty_ygui_widget_init_base(m, x, y, w, 2 * MENU_PAD_Y);
    m->data.popup_menu.item_labels = NULL;
    m->data.popup_menu.item_callbacks = NULL;
    m->data.popup_menu.item_userdata = NULL;
    m->data.popup_menu.n_items = 0;
    m->data.popup_menu.capacity = 0;
    m->data.popup_menu.item_h = 0.0f;
    m->data.popup_menu.modal = 0;
    m->data.popup_menu.hover_index = -1;
    m->flags &= ~YETTY_YGUI_FLAG_OPEN; /* start closed */
    m->vtable = &popup_menu_vtable;
    /* Position is absolute — menus pop up over everything else. */
    m->layout.position = YETTY_YGUI_POSITION_ABSOLUTE;
    yetty_ygui_engine_attach_widget(engine, m);
    return m;
}

static int menu_grow(struct yetty_ygui_widget *self, int need)
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
    if (!labels || !cbs || !udata) {
        /* Partial grows are fine — next call retries. Free nothing. */
        if (labels) self->data.popup_menu.item_labels = labels;
        if (cbs) self->data.popup_menu.item_callbacks = cbs;
        if (udata) self->data.popup_menu.item_userdata = udata;
        return 0;
    }
    self->data.popup_menu.item_labels = labels;
    self->data.popup_menu.item_callbacks = cbs;
    self->data.popup_menu.item_userdata = udata;
    self->data.popup_menu.capacity = cap;
    return 1;
}

void yetty_ygui_widget_popup_menu_add_item(struct yetty_ygui_widget *menu, const char *label,
                                           ygui_click_callback_t cb, void *userdata)
{
    if (!menu || menu->type != YETTY_YGUI_WIDGET_POPUP_MENU) {
        return;
    }
    if (!menu_grow(menu, menu->data.popup_menu.n_items + 1)) {
        return;
    }
    int i = menu->data.popup_menu.n_items;
    menu->data.popup_menu.item_labels[i] = ygui_strdup(label ? label : "");
    menu->data.popup_menu.item_callbacks[i] = cb;
    menu->data.popup_menu.item_userdata[i] = userdata;
    menu->data.popup_menu.n_items = i + 1;
    menu_resize(menu);
    if (menu->engine) {
        menu->engine->dirty = 1;
    }
}

void yetty_ygui_widget_popup_menu_add_separator(struct yetty_ygui_widget *menu)
{
    if (!menu || menu->type != YETTY_YGUI_WIDGET_POPUP_MENU) {
        return;
    }
    if (!menu_grow(menu, menu->data.popup_menu.n_items + 1)) {
        return;
    }
    int i = menu->data.popup_menu.n_items;
    menu->data.popup_menu.item_labels[i] = NULL;
    menu->data.popup_menu.item_callbacks[i] = NULL;
    menu->data.popup_menu.item_userdata[i] = NULL;
    menu->data.popup_menu.n_items = i + 1;
    menu_resize(menu);
    if (menu->engine) {
        menu->engine->dirty = 1;
    }
}

void yetty_ygui_widget_popup_menu_open_at(struct yetty_ygui_widget *menu, float x, float y)
{
    if (!menu || menu->type != YETTY_YGUI_WIDGET_POPUP_MENU) {
        return;
    }
    menu->authored_x = x;
    menu->authored_y = y;
    menu->x = x;
    menu->y = y;
    menu->layout_x = x;
    menu->layout_y = y;
    menu->flags |= YETTY_YGUI_FLAG_OPEN;
    if (menu->engine) {
        menu->engine->dirty = 1;
    }
}

void yetty_ygui_widget_popup_menu_close(struct yetty_ygui_widget *menu)
{
    if (!menu || menu->type != YETTY_YGUI_WIDGET_POPUP_MENU) {
        return;
    }
    menu->flags &= ~YETTY_YGUI_FLAG_OPEN;
    if (menu->engine) {
        menu->engine->dirty = 1;
    }
}

void yetty_ygui_widget_popup_menu_set_modal(struct yetty_ygui_widget *menu, int modal)
{
    if (!menu || menu->type != YETTY_YGUI_WIDGET_POPUP_MENU) {
        return;
    }
    menu->data.popup_menu.modal = modal ? 1 : 0;
}

int yetty_ygui_widget_popup_menu_is_open(const struct yetty_ygui_widget *menu)
{
    if (!menu || menu->type != YETTY_YGUI_WIDGET_POPUP_MENU) {
        return 0;
    }
    return (menu->flags & YETTY_YGUI_FLAG_OPEN) ? 1 : 0;
}
