/*
 * ygui_window.c — top-level WINDOW widget.
 *
 * A frame around an entire ygui app: title bar at the top with the app
 * name centred, a hamburger menu button in the upper-right corner, and
 * an auto-allocated body container below. Application code adds widgets
 * to the body (window_body()) instead of attaching them directly to
 * the window, so the title bar's pixels stay reserved.
 *
 * Hamburger behaviour
 *   When a popup menu has been attached via window_set_menu, the
 *   hamburger toggles its OPEN flag (and positions it under the
 *   button on first open). The menu then handles its own item
 *   dispatch. With no menu attached, the hamburger acts as a direct
 *   close button that calls engine_close_preserve() — keeping the
 *   last rendered frame on the ydraw canvas after the app exits.
 *
 *   The button uses the same square footprint as the tabbar's per-tab
 *   close 'x' (yetty_ygui_tabbar_button_size) so the two visually
 *   line up in the title bar / strip combo.
 *
 * Hit testing
 *   on_press: if the click lands in the hamburger box, fire the menu
 *   / close path. Otherwise return 0 so the click falls through to
 *   the body's children.
 */

#include "ygui_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yetty/ytrace/ytrace.h>

void yetty_ygui_engine_attach_widget(struct yetty_ygui_engine *engine,
                                     struct yetty_ygui_widget *widget);
float yetty_ygui_tabbar_button_size(void);

#define WINDOW_DEFAULT_TITLE_H 30.0f
#define WINDOW_BUTTON_PAD 6.0f
#define WINDOW_CORNER_RADIUS 8.0f
#define WINDOW_HAMBURGER_LINE_H 2.0f
#define WINDOW_HAMBURGER_INSET_X 3.0f
#define WINDOW_HAMBURGER_INSET_Y 4.0f

static float window_title_h(const struct yetty_ygui_widget *self)
{
    if (self->data.window.title_h > 0) {
        return self->data.window.title_h;
    }
    return WINDOW_DEFAULT_TITLE_H;
}

static float window_button_size(void)
{
    /* Match the tabbar's per-tab close X so the window close button
     * and tab close button line up visually. */
    return yetty_ygui_tabbar_button_size();
}

/* Hamburger button rect in window-local coordinates (relative to
 * self->x/y). Vertically centred in the title bar, pinned to the
 * right edge with a small pad. */
static void menu_button_rect(const struct yetty_ygui_widget *self, float *out_x, float *out_y,
                             float *out_w, float *out_h)
{
    float th = window_title_h(self);
    float sz = window_button_size();
    if (sz > th - 2 * WINDOW_BUTTON_PAD) {
        sz = th - 2 * WINDOW_BUTTON_PAD;
    }
    *out_w = sz;
    *out_h = sz;
    *out_x = self->w - sz - WINDOW_BUTTON_PAD;
    *out_y = (th - sz) * 0.5f;
}

static struct yetty_ycore_void_result window_render(struct yetty_ygui_widget *self,
                                                    struct yetty_ygui_render_ctx *ctx)
{
    const struct yetty_ygui_theme *theme = ctx->theme;
    float th = window_title_h(self);

    /* Skip the first frame in CANVAS_FIT mode: the engine defaults the
     * canvas to 1x1 until OSC 777780 returns the real pixel size, and
     * placing a button at (self->w - sz) lands at negative x — the
     * ydraw canvas rejects the AABB and logs an error. By the time
     * resize fires the geometry is sane. */
    float bsz = window_button_size();
    ydebug("window_render id=%s self=(%.1f,%.1f) size=%.1fx%.1f th=%.1f bsz=%.1f thr=%.1f",
           self->id ? self->id : "?", self->x, self->y, self->w, self->h, th, bsz,
           th + 2 * (bsz + WINDOW_BUTTON_PAD));
    if (self->w < (th + 2 * (bsz + WINDOW_BUTTON_PAD))) {
        ydebug("window_render EARLY-RETURN (self->w=%.1f < threshold=%.1f)", self->w,
               th + 2 * (bsz + WINDOW_BUTTON_PAD));
        return YETTY_OK_VOID();
    }

    /* Frame: a single rounded box behind everything; the title bar
     * sits as a solid band on top. */
    ydebug("window_render: frame box=(%.1f,%.1f,%.1f,%.1f)", self->x, self->y, self->w, self->h);
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, self->h, theme->bg_primary,
                                     WINDOW_CORNER_RADIUS);
    ydebug("window_render: titlebar box=(%.1f,%.1f,%.1f,%.1f)", self->x, self->y, self->w, th);
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, th, theme->bg_header,
                                     WINDOW_CORNER_RADIUS);
    /* Hairline at the bottom of the title bar separates header from body. */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y + th - 1.0f, self->w, 1.0f,
                                     theme->border_muted, 0.0f);

    /* Title text — left-padded, vertically centred. */
    const char *title = self->data.window.title;
    if (title && *title) {
        float fs = theme->font_size > 0 ? theme->font_size : 14.0f;
        float ty = self->y + (th - fs) * 0.5f;
        yetty_ygui_render_ctx_render_text(ctx, title, self->x + 14.0f, ty, theme->text_primary, fs);
    }

    /* Hamburger button — soft-bg square with three horizontal bars. */
    float cx, cy, cw, ch;
    menu_button_rect(self, &cx, &cy, &cw, &ch);
    float ax = self->x + cx;
    float ay = self->y + cy;
    int menu_open =
        self->data.window.menu && (self->data.window.menu->flags & YETTY_YGUI_FLAG_OPEN);
    uint32_t bg = menu_open ? theme->bg_hover : theme->bg_header;
    yetty_ygui_render_ctx_render_box(ctx, ax, ay, cw, ch, bg, WINDOW_CORNER_RADIUS * 0.5f);

    /* Three horizontal lines. Inset from the button's edges so the
     * bars don't kiss the corners. */
    float line_w = cw - 2 * WINDOW_HAMBURGER_INSET_X;
    float inner_top = ay + WINDOW_HAMBURGER_INSET_Y;
    float inner_bot = ay + ch - WINDOW_HAMBURGER_INSET_Y - WINDOW_HAMBURGER_LINE_H;
    float inner_mid = (inner_top + inner_bot) * 0.5f;
    uint32_t bar_color = theme->text_muted;
    yetty_ygui_render_ctx_render_box(ctx, ax + WINDOW_HAMBURGER_INSET_X, inner_top, line_w,
                                     WINDOW_HAMBURGER_LINE_H, bar_color, 1.0f);
    yetty_ygui_render_ctx_render_box(ctx, ax + WINDOW_HAMBURGER_INSET_X, inner_mid, line_w,
                                     WINDOW_HAMBURGER_LINE_H, bar_color, 1.0f);
    yetty_ygui_render_ctx_render_box(ctx, ax + WINDOW_HAMBURGER_INSET_X, inner_bot, line_w,
                                     WINDOW_HAMBURGER_LINE_H, bar_color, 1.0f);

    return YETTY_OK_VOID();
}

static int window_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)out;
    float cx, cy, cw, ch;
    menu_button_rect(self, &cx, &cy, &cw, &ch);
    if (lx >= cx && lx < cx + cw && ly >= cy && ly < cy + ch) {
        if (self->data.window.menu) {
            /* Toggle the attached menu. Open: position the menu just
             * under the hamburger button, right-edge aligned. Close:
             * drop the OPEN flag. */
            struct yetty_ygui_widget *m = self->data.window.menu;
            if (m->flags & YETTY_YGUI_FLAG_OPEN) {
                yetty_ygui_widget_popup_menu_close(m);
            } else {
                float button_abs_x = self->layout_x + cx;
                float button_abs_y = self->layout_y + cy;
                /* Right-edge align the menu with the button so it
                 * looks anchored. The menu's authored width is its
                 * natural width — keep it. */
                float menu_x = button_abs_x + cw - m->w;
                if (menu_x < self->layout_x + 4.0f) {
                    menu_x = self->layout_x + 4.0f;
                }
                float menu_y = button_abs_y + ch + 4.0f;
                yetty_ygui_widget_popup_menu_open_at(m, menu_x, menu_y);
            }
            return 1;
        }
        /* No menu attached — fall back to direct close. */
        if (self->data.window.on_close) {
            self->data.window.on_close(self, self->data.window.on_close_userdata);
        }
        if (self->engine) {
            yetty_ygui_engine_close_preserve(self->engine);
        }
        return 1;
    }

    /* Title-bar grab — anywhere in the title strip outside the
     * hamburger button starts a window drag. We record ABSOLUTE mouse
     * coords at press time (not the widget-local lx/ly the engine
     * passes in) because the engine recomputes lx as
     * `mouse_x - effective_x` on every move, and effective_x changes
     * as soon as we move the window, so widget-local deltas accumulate
     * a one-frame lag and the window drifts behind the cursor (visible
     * as flicker). lx + self->effective_x is the invariant: it always
     * equals the absolute mouse_x regardless of any intervening
     * layout. */
    float th = window_title_h(self);
    if (ly >= 0 && ly < th) {
        self->data.window.dragging = 1;
        self->data.window.drag_press_lx = lx + self->effective_x;
        self->data.window.drag_press_ly = ly + self->effective_y;
        self->data.window.drag_orig_x = self->x;
        self->data.window.drag_orig_y = self->y;
        return 1;
    }
    return 0;
}

static int window_on_drag(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)out;
    if (!self->data.window.dragging) {
        return 0;
    }
    /* Absolute-mouse delta — see comment in window_on_press for why we
     * can't use widget-local lx/ly directly. */
    float mouse_x = lx + self->effective_x;
    float mouse_y = ly + self->effective_y;
    float dx = mouse_x - self->data.window.drag_press_lx;
    float dy = mouse_y - self->data.window.drag_press_ly;
    self->x = self->data.window.drag_orig_x + dx;
    self->y = self->data.window.drag_orig_y + dy;
    self->authored_x = self->x;
    self->authored_y = self->y;
    self->dirty = 1;
    if (self->engine) {
        self->engine->dirty = 1;
    }
    return 1;
}

static int window_on_release(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)lx;
    (void)ly;
    (void)out;
    self->data.window.dragging = 0;
    return 0;
}

static void window_destroy(struct yetty_ygui_widget *self)
{
    free(self->data.window.title);
    self->data.window.title = NULL;
    /* body is in the regular child hierarchy; freed via widget_free. */
}

static const struct yetty_ygui_widget_vtable window_vtable = {
    .render = window_render,
    .on_press = window_on_press,
    .on_drag = window_on_drag,
    .on_release = window_on_release,
    .destroy = window_destroy,
};

struct yetty_ygui_widget *yetty_ygui_engine_window(struct yetty_ygui_engine *engine, const char *id,
                                                   float x, float y, float w, float h,
                                                   const char *title)
{
    struct yetty_ygui_widget *win =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_WINDOW, id);
    if (!win) {
        return NULL;
    }
    yetty_ygui_widget_init_base(win, x, y, w, h);
    win->data.window.title = title ? ygui_strdup(title) : NULL;
    win->data.window.title_h = 0.0f;
    win->data.window.body = NULL;
    win->data.window.on_close = NULL;
    win->data.window.on_close_userdata = NULL;
    win->data.window.menu = NULL;
    win->vtable = &window_vtable;

    /* Default layout: flex column with padding-top equal to the title
     * bar so children land below the header. align-items: stretch so
     * the body fills the width. */
    win->layout.mode = YETTY_YGUI_LAYOUT_FLEX;
    win->layout.direction = YETTY_YGUI_FLEX_COLUMN;
    win->layout.align_items = YETTY_YGUI_ALIGN_STRETCH;
    win->layout.padding_top = WINDOW_DEFAULT_TITLE_H;

    yetty_ygui_engine_attach_widget(engine, win);

    /* Auto-allocate the body container — a vbox that grows to fill the
     * remaining space inside the window's content box. App code adds
     * children here via window_body(). */
    char body_id[256];
    snprintf(body_id, sizeof(body_id), "%s__body", id ? id : "window");
    struct yetty_ygui_widget *body = yetty_ygui_engine_vbox(engine, body_id, 0, 0, 0, 0);
    if (body) {
        body->layout.mode = YETTY_YGUI_LAYOUT_FLEX;
        body->layout.direction = YETTY_YGUI_FLEX_COLUMN;
        body->layout.align_items = YETTY_YGUI_ALIGN_STRETCH;
        body->layout.flex_grow = 1.0f;
        body->layout.flex_shrink = 1.0f;
        yetty_ygui_widget_add_child(win, body);
        win->data.window.body = body;
    }
    return win;
}

struct yetty_ygui_widget *yetty_ygui_widget_window_body(struct yetty_ygui_widget *window)
{
    if (!window || window->type != YETTY_YGUI_WIDGET_WINDOW) {
        return NULL;
    }
    return window->data.window.body;
}

void yetty_ygui_widget_window_set_title(struct yetty_ygui_widget *window, const char *title)
{
    if (!window || window->type != YETTY_YGUI_WIDGET_WINDOW) {
        return;
    }
    free(window->data.window.title);
    window->data.window.title = title ? ygui_strdup(title) : NULL;
    if (window->engine) {
        window->engine->dirty = 1;
        window->dirty = 1;
    }
}

const char *yetty_ygui_widget_window_get_title(const struct yetty_ygui_widget *window)
{
    if (!window || window->type != YETTY_YGUI_WIDGET_WINDOW) {
        return NULL;
    }
    return window->data.window.title;
}

void yetty_ygui_widget_window_on_close(struct yetty_ygui_widget *window,
                                       ygui_click_callback_t callback, void *userdata)
{
    if (!window || window->type != YETTY_YGUI_WIDGET_WINDOW) {
        return;
    }
    window->data.window.on_close = callback;
    window->data.window.on_close_userdata = userdata;
}

void yetty_ygui_engine_close_preserve(struct yetty_ygui_engine *engine)
{
    if (!engine) {
        return;
    }
    engine->preserve_canvas_on_destroy = 1;
    engine->running = 0;
}

void yetty_ygui_widget_window_set_menu(struct yetty_ygui_widget *window,
                                       struct yetty_ygui_widget *menu)
{
    if (!window || window->type != YETTY_YGUI_WIDGET_WINDOW) {
        return;
    }
    window->data.window.menu = menu;
    if (window->engine) {
        window->engine->dirty = 1;
        window->dirty = 1;
    }
}
