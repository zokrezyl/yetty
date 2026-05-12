/*
 * ygui_tabbar.c — TABBAR widget.
 *
 * Chrome-style tab strip across the top of the widget's box. One content
 * panel per tab below; only the active panel is rendered and laid out.
 * Clicking a header pill switches the active tab and fires on_change with
 * the new index in `value` via the standard change-callback machinery
 * (same wire slider/checkbox use).
 *
 * The tabbar itself is a flex-column container — its content panels are
 * the visible flex children. The header strip is drawn directly by the
 * tabbar's render fn (inline SDF boxes + text spans), with hit-testing
 * handled by on_press. The header reserves `header_h` pixels at the top
 * via padding-top on the container, so content panels naturally lay out
 * below the strip without manual offset math.
 */

#include "ygui_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Shared engine-attachment helper (see ygui_widgets.c). */
void yetty_ygui_engine_attach_widget(struct yetty_ygui_engine *engine,
                                     struct yetty_ygui_widget *widget);

/*=============================================================================
 * Geometry helpers
 *===========================================================================*/

#define TABBAR_DEFAULT_HEADER_H 32.0f
#define TABBAR_PILL_PAD_X 14.0f
#define TABBAR_PILL_GAP 4.0f
#define TABBAR_PILL_RADIUS 6.0f
#define TABBAR_ACCENT_BAR_H 3.0f

static float tabbar_header_h(const struct yetty_ygui_widget *self)
{
    if (self->data.tabbar.header_h > 0) {
        return self->data.tabbar.header_h;
    }
    return TABBAR_DEFAULT_HEADER_H;
}

static float tabbar_pill_width(const struct yetty_ygui_widget *self, int i)
{
    /* Equal-width pills: divide header width by tab count, clamp to a
     * minimum so a single label can always fit. Pill content (label)
     * truncates on overflow by the underlying text path. */
    int n = self->data.tabbar.n_tabs;
    (void)i;
    if (n <= 0) {
        return 0.0f;
    }
    float total_gap = (float)(n - 1) * TABBAR_PILL_GAP;
    float each = (self->layout_w - total_gap) / (float)n;
    float min_w = 60.0f;
    if (each < min_w) {
        each = min_w;
    }
    return each;
}

/*=============================================================================
 * Render — draw the header strip. Content panels render themselves via the
 * normal child traversal in render_all_default; only the active panel is
 * visible, so the strip is the only thing this widget owns visually.
 *===========================================================================*/

static struct yetty_ycore_void_result tabbar_render(struct yetty_ygui_widget *self,
                                                    struct yetty_ygui_render_ctx *ctx)
{
    if (self->data.tabbar.n_tabs <= 0) {
        return YETTY_OK_VOID();
    }
    const struct yetty_ygui_theme *theme = ctx->theme;
    float hh = tabbar_header_h(self);

    /* Strip background — a flat band the width of the widget. Drawn at
     * the widget's local origin (render-ctx offset_x/y already includes
     * the parent's absolute position). */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, hh, theme->bg_surface,
                                     0.0f);

    float x = self->x;
    float y = self->y;
    float pw = tabbar_pill_width(self, 0);
    for (int i = 0; i < self->data.tabbar.n_tabs; i++) {
        int active = (i == self->data.tabbar.active);
        uint32_t fill = active ? theme->bg_primary : theme->bg_surface;
        uint32_t text_color = active ? theme->text_primary : theme->text_muted;

        yetty_ygui_render_ctx_render_box(ctx, x, y, pw, hh, fill, TABBAR_PILL_RADIUS);

        if (active) {
            yetty_ygui_render_ctx_render_box(ctx, x, y + hh - TABBAR_ACCENT_BAR_H, pw,
                                             TABBAR_ACCENT_BAR_H, theme->accent, 0.0f);
        }

        const char *label = self->data.tabbar.labels[i];
        if (label && *label) {
            float fs = theme->font_size > 0 ? theme->font_size : 14.0f;
            /* render_text places the text TOP at (x, y) — see
             * src/yetty/ygui/ygui_widgets.c label_render comments. The
             * label descends from y to y+fs, so centring vertically is
             * just splitting the leftover space. Without the
             * (now-removed) extra +fs*0.8 baseline shift the descender
             * cleared the accent bar at the bottom of the pill. */
            float tx = x + TABBAR_PILL_PAD_X;
            float ty = y + (hh - fs) * 0.5f;
            /* Reserve a few pixels of headroom above the active accent
             * bar so descenders never paint over it. */
            float max_top = y + hh - TABBAR_ACCENT_BAR_H - fs - 2.0f;
            if (ty > max_top) {
                ty = max_top;
            }
            yetty_ygui_render_ctx_render_text(ctx, label, tx, ty, text_color, fs);
        }

        x += pw + TABBAR_PILL_GAP;
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Hit-test the header strip on press; swap active tab.
 *===========================================================================*/

static int tabbar_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)out;
    float hh = tabbar_header_h(self);
    if (ly < 0 || ly > hh) {
        return 0; /* press below the strip — let it fall through to children */
    }
    if (self->data.tabbar.n_tabs <= 0) {
        return 0;
    }
    float pw = tabbar_pill_width(self, 0);
    float x = 0.0f;
    for (int i = 0; i < self->data.tabbar.n_tabs; i++) {
        if (lx >= x && lx < x + pw) {
            if (i != self->data.tabbar.active) {
                yetty_ygui_widget_tabbar_set_active(self, i);
            }
            return 1;
        }
        x += pw + TABBAR_PILL_GAP;
    }
    return 0;
}

/*=============================================================================
 * Destroy
 *===========================================================================*/

static void tabbar_destroy(struct yetty_ygui_widget *self)
{
    if (self->data.tabbar.labels) {
        for (int i = 0; i < self->data.tabbar.n_tabs; i++) {
            free(self->data.tabbar.labels[i]);
        }
        free(self->data.tabbar.labels);
        self->data.tabbar.labels = NULL;
    }
    /* panels are normal children and are freed via the widget hierarchy
     * in yetty_ygui_widget_free. We just free our parallel index array. */
    if (self->data.tabbar.panels) {
        free(self->data.tabbar.panels);
        self->data.tabbar.panels = NULL;
    }
    self->data.tabbar.n_tabs = 0;
    self->data.tabbar.capacity = 0;
}

static const struct yetty_ygui_widget_vtable tabbar_vtable = {
    .render = tabbar_render,
    .on_press = tabbar_on_press,
    .destroy = tabbar_destroy,
};

/*=============================================================================
 * Public API
 *===========================================================================*/

struct yetty_ygui_widget *yetty_ygui_engine_tabbar(struct yetty_ygui_engine *engine, const char *id,
                                                   float x, float y, float w, float h)
{
    struct yetty_ygui_widget *t =
        yetty_ygui_engine_widget_alloc(engine, YETTY_YGUI_WIDGET_TABBAR, id);
    if (!t) {
        return NULL;
    }
    yetty_ygui_widget_init_base(t, x, y, w, h);
    t->data.tabbar.labels = NULL;
    t->data.tabbar.panels = NULL;
    t->data.tabbar.n_tabs = 0;
    t->data.tabbar.capacity = 0;
    t->data.tabbar.active = -1;
    t->data.tabbar.header_h = 0.0f; /* derive from theme/default */
    t->vtable = &tabbar_vtable;

    /* Default layout: flex column with padding-top equal to the header so
     * content panels lay out cleanly below the strip. CSS callers can
     * override. */
    t->layout.mode = YETTY_YGUI_LAYOUT_FLEX;
    t->layout.direction = YETTY_YGUI_FLEX_COLUMN;
    t->layout.align_items = YETTY_YGUI_ALIGN_STRETCH;
    t->layout.padding_top = TABBAR_DEFAULT_HEADER_H;

    yetty_ygui_engine_attach_widget(engine, t);
    return t;
}

static int tabbar_grow_capacity(struct yetty_ygui_widget *self, int need)
{
    if (need <= self->data.tabbar.capacity) {
        return 1;
    }
    int cap = self->data.tabbar.capacity ? self->data.tabbar.capacity * 2 : 4;
    while (cap < need) {
        cap *= 2;
    }
    char **labels = (char **)realloc(self->data.tabbar.labels, (size_t)cap * sizeof(char *));
    if (!labels) {
        return 0;
    }
    struct yetty_ygui_widget **panels = (struct yetty_ygui_widget **)realloc(
        self->data.tabbar.panels, (size_t)cap * sizeof(struct yetty_ygui_widget *));
    if (!panels) {
        /* labels still grew but we keep the larger allocation — harmless.
         * Bail before assigning so we don't half-grow the parallel arrays. */
        self->data.tabbar.labels = labels;
        return 0;
    }
    self->data.tabbar.labels = labels;
    self->data.tabbar.panels = panels;
    self->data.tabbar.capacity = cap;
    return 1;
}

struct yetty_ygui_widget *yetty_ygui_widget_tabbar_add_tab(struct yetty_ygui_widget *tabbar,
                                                           const char *label)
{
    if (!tabbar || tabbar->type != YETTY_YGUI_WIDGET_TABBAR) {
        return NULL;
    }
    if (!tabbar_grow_capacity(tabbar, tabbar->data.tabbar.n_tabs + 1)) {
        return NULL;
    }
    /* Compose a stable per-tab id: <tabbar-id>__tab<index>. */
    char panel_id[256];
    snprintf(panel_id, sizeof(panel_id), "%s__tab%d", tabbar->id ? tabbar->id : "tabbar",
             tabbar->data.tabbar.n_tabs);

    struct yetty_ygui_widget *panel =
        yetty_ygui_engine_vbox(tabbar->engine, panel_id, 0, 0, 0, 0);
    if (!panel) {
        return NULL;
    }
    panel->layout.mode = YETTY_YGUI_LAYOUT_FLEX;
    panel->layout.direction = YETTY_YGUI_FLEX_COLUMN;
    panel->layout.flex_grow = 1.0f;
    panel->layout.flex_shrink = 1.0f;
    panel->layout.align_items = YETTY_YGUI_ALIGN_STRETCH;

    int idx = tabbar->data.tabbar.n_tabs;
    tabbar->data.tabbar.labels[idx] = ygui_strdup(label ? label : "");
    tabbar->data.tabbar.panels[idx] = panel;
    tabbar->data.tabbar.n_tabs = idx + 1;

    yetty_ygui_widget_add_child(tabbar, panel);

    /* First tab becomes active automatically. */
    if (tabbar->data.tabbar.active < 0) {
        tabbar->data.tabbar.active = idx;
    } else if (idx != tabbar->data.tabbar.active) {
        yetty_ygui_widget_set_visible(panel, 0);
    }

    if (tabbar->engine) {
        tabbar->engine->dirty = 1;
    }
    return panel;
}

void yetty_ygui_widget_tabbar_set_active(struct yetty_ygui_widget *tabbar, int index)
{
    if (!tabbar || tabbar->type != YETTY_YGUI_WIDGET_TABBAR) {
        return;
    }
    if (index < 0 || index >= tabbar->data.tabbar.n_tabs) {
        return;
    }
    if (index == tabbar->data.tabbar.active) {
        return;
    }
    int prev = tabbar->data.tabbar.active;
    if (prev >= 0 && prev < tabbar->data.tabbar.n_tabs && tabbar->data.tabbar.panels[prev]) {
        yetty_ygui_widget_set_visible(tabbar->data.tabbar.panels[prev], 0);
    }
    tabbar->data.tabbar.active = index;
    if (tabbar->data.tabbar.panels[index]) {
        yetty_ygui_widget_set_visible(tabbar->data.tabbar.panels[index], 1);
    }
    if (tabbar->change_callback) {
        tabbar->change_callback(tabbar, (float)index, tabbar->change_userdata);
    }
    if (tabbar->engine) {
        tabbar->engine->dirty = 1;
    }
}

int yetty_ygui_widget_tabbar_get_active(const struct yetty_ygui_widget *tabbar)
{
    if (!tabbar || tabbar->type != YETTY_YGUI_WIDGET_TABBAR) {
        return -1;
    }
    return tabbar->data.tabbar.active;
}

int yetty_ygui_widget_tabbar_count(const struct yetty_ygui_widget *tabbar)
{
    if (!tabbar || tabbar->type != YETTY_YGUI_WIDGET_TABBAR) {
        return 0;
    }
    return tabbar->data.tabbar.n_tabs;
}

void yetty_ygui_widget_tabbar_on_change(struct yetty_ygui_widget *tabbar,
                                        ygui_change_callback_t callback, void *userdata)
{
    if (!tabbar || tabbar->type != YETTY_YGUI_WIDGET_TABBAR) {
        return;
    }
    tabbar->change_callback = callback;
    tabbar->change_userdata = userdata;
}
