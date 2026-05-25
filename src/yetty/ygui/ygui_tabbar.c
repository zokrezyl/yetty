/*
 * ygui_tabbar.c — TABBAR widget.
 *
 * Browser-style tab strip across the top of the widget's box. One content
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
 *
 * Visual conventions (typical browser-tab style to keep the strip readable):
 *   - The active tab uses the primary background colour and a coloured
 *     accent bar at the bottom; inactive tabs share the surface
 *     background.
 *   - Between two ADJACENT INACTIVE tabs a thin vertical separator is
 *     drawn so they don't blur into one solid band. The separator is
 *     deliberately omitted next to the active tab — that's how most
 *     browser tab strips give the active tab visual breathing room.
 *   - Each pill carries a small close 'x' button on its right side
 *     (yetty_ygui_widget_tabbar_remove_tab on click). The hit-box size
 *     matches the YETTY_YGUI_TABBAR_BUTTON_SIZE constant so the window
 *     widget's hamburger button can reuse the same geometry.
 */

#include "ygui_internal.h"

#include <yetty/ytrace/ytrace.h>
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

/* Preferred / minimum per-pill width. Matches typical browser tab
 * strips: ~160 px when there's room, shrink-floor ~80 px before tabs
 * become illegible. Tabs DO NOT stretch beyond the preferred width
 * when there's spare room — the strip is allowed to leave a gap on
 * the right (filled by the optional "+" pill or just empty space). */
#define TABBAR_PILL_PREF_W 160.0f
#define TABBAR_PILL_MIN_W 80.0f

/* Per-tab close-button + window hamburger button size. Kept as
 * a public constant via ygui_internal.h's TABBAR_BUTTON_SIZE so the
 * window widget can match it without copy-pasting magic numbers. */
#define TABBAR_BUTTON_SIZE 18.0f
#define TABBAR_BUTTON_PAD 6.0f

/* Width of the separator between two adjacent inactive tabs. Thin
 * hairline; visible enough to break up the surface but unobtrusive. */
#define TABBAR_SEPARATOR_W 1.0f
#define TABBAR_SEPARATOR_INSET_Y 6.0f

/* Footprint reserved for the optional "+" new-tab pill that sits
 * immediately after the last tab when on_new_tab is set. Matches the
 * close-x button footprint so the right edge reads consistently. */
#define TABBAR_NEWTAB_W 28.0f
#define TABBAR_NEWTAB_PAD 6.0f

float yetty_ygui_tabbar_button_size(void)
{
    return TABBAR_BUTTON_SIZE;
}

static float tabbar_header_h(const struct yetty_ygui_widget *self)
{
    float h = self->data.tabbar.header_h > 0 ? self->data.tabbar.header_h : TABBAR_DEFAULT_HEADER_H;
    /* Never draw a header that's taller than the widget itself, otherwise
     * the strip overflows downward and the layout pass places panel
     * children (content_y = layout_y + padding_top) outside the tabbar's
     * own box — siblings end up overlapping. */
    if (self->layout_h > 0 && h > self->layout_h) {
        h = self->layout_h;
    }
    return h;
}

static float tabbar_pill_width(const struct yetty_ygui_widget *self)
{
    int n = self->data.tabbar.n_tabs;
    if (n <= 0) {
        return 0.0f;
    }
    float total_gap = (float)(n - 1) * TABBAR_PILL_GAP;
    /* Reserve room for the optional "+" pill so the tabs don't share
     * its slot. The pill itself sits in the gap after the last tab,
     * so we subtract its full footprint (button + the gap before it). */
    float reserved = self->data.tabbar.on_new_tab ? (TABBAR_NEWTAB_W + TABBAR_PILL_GAP) : 0.0f;
    /* Browser-style sizing: each tab gets its preferred width up to
     * the available room. Only when n × pref + gaps exceeds the
     * strip width does the algorithm shrink the pills uniformly. The
     * floor is the minimum legibility width; below that the tabs
     * stop shrinking and clip at the right edge. */
    float avail = self->layout_w - total_gap - reserved;
    float per_pref = TABBAR_PILL_PREF_W;
    if (per_pref * (float)n > avail) {
        per_pref = avail / (float)n;
    }
    if (per_pref < TABBAR_PILL_MIN_W) {
        per_pref = TABBAR_PILL_MIN_W;
    }
    return per_pref;
}

/* Hit rect for the "+" pill in tabbar-local coords (relative to the
 * widget's top-left). Returns the pill's (x, y, w, h); caller must
 * check `on_new_tab` non-NULL first. */
static void tabbar_newtab_rect(const struct yetty_ygui_widget *self, float pill_w, float *out_x,
                               float *out_y, float *out_w, float *out_h)
{
    float hh = tabbar_header_h(self);
    float tabs_end = (float)self->data.tabbar.n_tabs * pill_w +
                     (float)(self->data.tabbar.n_tabs - 1) * TABBAR_PILL_GAP;
    if (self->data.tabbar.n_tabs <= 0) {
        tabs_end = 0.0f;
    }
    float sz = TABBAR_BUTTON_SIZE;
    if (sz > hh - 2 * TABBAR_NEWTAB_PAD) {
        sz = hh - 2 * TABBAR_NEWTAB_PAD;
    }
    *out_w = TABBAR_NEWTAB_W;
    *out_h = sz;
    *out_x = tabs_end + TABBAR_PILL_GAP;
    *out_y = (hh - sz) * 0.5f;
}

/* Per-tab close-box rect in window-local coordinates (relative to the
 * pill's top-left, which is the (x, y) passed in). The close box is
 * pinned to the right edge of the pill, vertically centred in the
 * header strip. */
static void tab_close_rect(float pill_w, float hh, float *out_x, float *out_y, float *out_w,
                           float *out_h)
{
    float sz = TABBAR_BUTTON_SIZE;
    if (sz > hh - 2 * TABBAR_BUTTON_PAD) {
        sz = hh - 2 * TABBAR_BUTTON_PAD;
    }
    *out_w = sz;
    *out_h = sz;
    *out_x = pill_w - sz - TABBAR_BUTTON_PAD;
    *out_y = (hh - sz) * 0.5f;
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
    ydebug(
        "tabbar_render id=%s self=(x=%.1f y=%.1f w=%.1f h=%.1f) layout=(%.1f,%.1f %.1fx%.1f) "
        "content=(%.1f,%.1f %.1fx%.1f) pad_top=%.1f hh=%.1f offset=(%.1f,%.1f) n_tabs=%d active=%d",
        self->id ? self->id : "?", self->x, self->y, self->w, self->h, self->layout_x,
        self->layout_y, self->layout_w, self->layout_h, self->content_x, self->content_y,
        self->content_w, self->content_h, self->layout.padding_top, hh, ctx->offset_x,
        ctx->offset_y, self->data.tabbar.n_tabs, self->data.tabbar.active);
    for (int pi = 0; pi < self->data.tabbar.n_tabs; pi++) {
        struct yetty_ygui_widget *panel = self->data.tabbar.panels[pi];
        if (panel) {
            ydebug("tabbar_render: panel[%d] id=%s vis=%d layout=(%.1f,%.1f %.1fx%.1f) "
                   "rel=(%.1f,%.1f %.1fx%.1f)",
                   pi, panel->id ? panel->id : "?",
                   (panel->flags & YETTY_YGUI_FLAG_VISIBLE) ? 1 : 0, panel->layout_x,
                   panel->layout_y, panel->layout_w, panel->layout_h, panel->x, panel->y, panel->w,
                   panel->h);
        }
    }

    /* Strip background — a flat band the width of the widget. */
    yetty_ygui_render_ctx_render_box(ctx, self->x, self->y, self->w, hh, theme->bg_surface, 0.0f);

    float x = self->x;
    float y = self->y;
    float pw = tabbar_pill_width(self);
    int active = self->data.tabbar.active;

    for (int i = 0; i < self->data.tabbar.n_tabs; i++) {
        int is_active = (i == active);
        uint32_t fill = is_active ? theme->bg_primary : theme->bg_surface;
        uint32_t text_color = is_active ? theme->text_primary : theme->text_muted;

        yetty_ygui_render_ctx_render_box(ctx, x, y, pw, hh, fill, TABBAR_PILL_RADIUS);

        if (is_active) {
            yetty_ygui_render_ctx_render_box(ctx, x, y + hh - TABBAR_ACCENT_BAR_H, pw,
                                             TABBAR_ACCENT_BAR_H, theme->accent, 0.0f);
        }

        /* Browser-style separator: draw between this pill and the next
         * ONLY if both are inactive. The active tab gets visual
         * breathing room on both sides, which is how typical browser
         * tab strips read. */
        if (i + 1 < self->data.tabbar.n_tabs && !is_active && (i + 1) != active) {
            float sep_x = x + pw + (TABBAR_PILL_GAP - TABBAR_SEPARATOR_W) * 0.5f;
            float sep_y = y + TABBAR_SEPARATOR_INSET_Y;
            float sep_h = hh - 2 * TABBAR_SEPARATOR_INSET_Y;
            yetty_ygui_render_ctx_render_box(ctx, sep_x, sep_y, TABBAR_SEPARATOR_W, sep_h,
                                             theme->border_muted, 0.0f);
        }

        /* Label. The text path renders top-aligned at (tx, ty); the
         * label sits to the left of the close 'x' button (we leave a
         * pad of TABBAR_BUTTON_SIZE + 2*TABBAR_BUTTON_PAD on the right
         * so the two don't visually collide). */
        const char *label = self->data.tabbar.labels[i];
        ydebug("tabbar_render: pill[%d] label=%s pill_xy=(%.1f,%.1f) pw=%.1f", i,
               label ? label : "(null)", x, y, pw);
        if (label && *label) {
            float fs = theme->font_size > 0 ? theme->font_size : 14.0f;
            float tx = x + TABBAR_PILL_PAD_X;
            float ty = y + (hh - fs) * 0.5f;
            float max_top = y + hh - TABBAR_ACCENT_BAR_H - fs - 2.0f;
            if (ty > max_top) {
                ty = max_top;
            }
            ydebug("tabbar_render: pill[%d] emit text '%s' at (%.1f,%.1f) color=0x%08x fs=%.1f", i,
                   label, tx, ty, text_color, fs);
            yetty_ygui_render_ctx_render_text(ctx, label, tx, ty, text_color, fs);
        }

        /* Close 'x' button on the right edge of the pill. Always paint
         * a subtle background square so the click target is visible
         * even on inactive tabs — previously the inactive close
         * affordance was just a small glyph floating in space, easy to
         * miss when aiming. Active tabs get a slightly stronger fill
         * to keep the visual emphasis on the focused pill. */
        float cx, cy, cw, ch;
        tab_close_rect(pw, hh, &cx, &cy, &cw, &ch);
        float ax = x + cx;
        float ay = y + cy;
        uint32_t close_bg = is_active ? theme->bg_surface : theme->bg_hover;
        yetty_ygui_render_ctx_render_box(ctx, ax, ay, cw, ch, close_bg, TABBAR_PILL_RADIUS * 0.5f);
        float gfs = ch * 0.85f;
        if (gfs < 10.0f) {
            gfs = 10.0f;
        }
        float gx = ax + (cw - gfs * 0.5f) * 0.5f;
        float gy = ay + (ch - gfs) * 0.5f - 1.0f;
        yetty_ygui_render_ctx_render_text(ctx, "x", gx, gy, theme->text_primary, gfs);

        x += pw + TABBAR_PILL_GAP;
    }

    /* Optional "+" new-tab pill — small rounded square just past the
     * last tab. Same brand-accent glyph as the per-tab close 'x' so
     * the strip's clickable affordances feel related. */
    if (self->data.tabbar.on_new_tab) {
        float nx, ny, nw, nh;
        tabbar_newtab_rect(self, pw, &nx, &ny, &nw, &nh);
        float ax = self->x + nx;
        float ay = self->y + ny;
        yetty_ygui_render_ctx_render_box(ctx, ax, ay, nw, nh, theme->bg_hover,
                                         TABBAR_PILL_RADIUS * 0.5f);
        float fs = nh * 0.85f;
        if (fs < 12.0f) {
            fs = 12.0f;
        }
        /* "+" is roughly font_size/2 wide — center it inside the pill. */
        float gx = ax + (nw - fs * 0.5f) * 0.5f;
        float gy = ay + (nh - fs) * 0.5f - 1.0f;
        yetty_ygui_render_ctx_render_text(ctx, "+", gx, gy,
                                          theme->accent ? theme->accent : theme->text_primary, fs);
    }
    return YETTY_OK_VOID();
}

/*=============================================================================
 * Hit-test. Close box wins over the rest of the pill so a user can
 * close a tab without switching to it first.
 *===========================================================================*/

static int tabbar_on_press(struct yetty_ygui_widget *self, float lx, float ly, ygui_event_t *out)
{
    (void)out;
    float hh = tabbar_header_h(self);
    ydebug("tabbar_on_press lx=%.1f ly=%.1f hh=%.1f n_tabs=%d", lx, ly, hh,
           self->data.tabbar.n_tabs);
    if (ly < 0 || ly > hh) {
        return 0;
    }
    float pw = tabbar_pill_width(self);

    /* "+" pill takes priority — even when there are no tabs yet, the
     * user should be able to spawn the first one. Hit-test in tabbar-
     * local coords. */
    if (self->data.tabbar.on_new_tab) {
        float nx, ny, nw, nh;
        tabbar_newtab_rect(self, pw, &nx, &ny, &nw, &nh);
        if (lx >= nx && lx < nx + nw && ly >= ny && ly < ny + nh) {
            self->data.tabbar.on_new_tab(self, self->data.tabbar.on_new_tab_userdata);
            return 1;
        }
    }

    if (self->data.tabbar.n_tabs <= 0) {
        return 0;
    }
    float x = 0.0f;
    for (int i = 0; i < self->data.tabbar.n_tabs; i++) {
        if (lx >= x && lx < x + pw) {
            float cx, cy, cw, ch;
            tab_close_rect(pw, hh, &cx, &cy, &cw, &ch);
            float gx = x + cx;
            ydebug("tabbar_on_press pill=%d pill_x=[%.1f..%.1f] "
                   "close_box=[%.1f..%.1f]x[%.1f..%.1f] hit_close=%d",
                   i, x, x + pw, gx, gx + cw, cy, cy + ch,
                   (lx >= gx && lx < gx + cw && ly >= cy && ly < cy + ch) ? 1 : 0);
            if (lx >= gx && lx < gx + cw && ly >= cy && ly < cy + ch) {
                /* Hit the close button. If the app registered an
                 * on_tab_close handler, it owns the decision — the
                 * tabbar fires the callback and stops. The app must
                 * call yetty_ygui_widget_tabbar_remove_tab() itself
                 * (typically after updating any external state that
                 * still references the closed tab's widgets, e.g. a
                 * parallel array of per-tab pointers — see the
                 * ygreeter tool for an example). When no handler is
                 * registered the tabbar applies the auto-remove
                 * default so simple uses just work. */
                if (self->data.tabbar.on_tab_close) {
                    self->data.tabbar.on_tab_close(self, (float)i,
                                                   self->data.tabbar.on_tab_close_userdata);
                } else {
                    yetty_ygui_widget_tabbar_remove_tab(self, i);
                }
                return 1;
            }
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
    /* panels are normal children freed via the widget hierarchy in
     * yetty_ygui_widget_free; only the parallel index array is ours. */
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
    t->data.tabbar.header_h = 0.0f;
    t->data.tabbar.on_tab_close = NULL;
    t->data.tabbar.on_tab_close_userdata = NULL;
    t->data.tabbar.on_new_tab = NULL;
    t->data.tabbar.on_new_tab_userdata = NULL;
    t->vtable = &tabbar_vtable;

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
    char panel_id[256];
    snprintf(panel_id, sizeof(panel_id), "%s__tab%d", tabbar->id ? tabbar->id : "tabbar",
             tabbar->data.tabbar.n_tabs);

    struct yetty_ygui_widget *panel = yetty_ygui_engine_vbox(tabbar->engine, panel_id, 0, 0, 0, 0);
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

    if (tabbar->data.tabbar.active < 0) {
        tabbar->data.tabbar.active = idx;
    } else if (idx != tabbar->data.tabbar.active) {
        yetty_ygui_widget_set_visible(panel, 0);
    }

    if (tabbar->engine) {
        tabbar->engine->dirty = 1;
        tabbar->dirty = 1;
    }
    return panel;
}

void yetty_ygui_widget_tabbar_remove_tab(struct yetty_ygui_widget *tabbar, int index)
{
    if (!tabbar || tabbar->type != YETTY_YGUI_WIDGET_TABBAR) {
        return;
    }
    if (index < 0 || index >= tabbar->data.tabbar.n_tabs) {
        return;
    }

    ydebug("tabbar_remove_tab enter index=%d n_tabs=%d active=%d", index,
           tabbar->data.tabbar.n_tabs, tabbar->data.tabbar.active);

    /* yetty_ygui_widget_remove unlinks the panel from its parent (us)
     * AND frees it. The earlier code in this function ALSO called
     * widget_free directly after widget_remove, which double-freed
     * everything below the panel and triggered "free(): double free
     * detected in tcache 2". widget_remove alone is the correct
     * complete-removal API. */
    struct yetty_ygui_widget *panel = tabbar->data.tabbar.panels[index];
    if (panel) {
        ydebug("tabbar_remove_tab: remove+free panel id=%s ptr=%p", panel->id ? panel->id : "?",
               (void *)panel);
        yetty_ygui_widget_remove(panel);
    }
    free(tabbar->data.tabbar.labels[index]);

    /* Compact the parallel arrays. */
    int n = tabbar->data.tabbar.n_tabs;
    for (int i = index; i < n - 1; i++) {
        tabbar->data.tabbar.labels[i] = tabbar->data.tabbar.labels[i + 1];
        tabbar->data.tabbar.panels[i] = tabbar->data.tabbar.panels[i + 1];
    }
    tabbar->data.tabbar.labels[n - 1] = NULL;
    tabbar->data.tabbar.panels[n - 1] = NULL;
    tabbar->data.tabbar.n_tabs = n - 1;

    /* Re-anchor the active index. If the removed tab WAS active, move
     * to the next one (or the previous one when removing the last
     * tab). If it sat after the active tab the index is unchanged but
     * the array shifted, which is already handled by the active index
     * being a position rather than a pointer — adjust only when the
     * removed index <= active. */
    int new_active = tabbar->data.tabbar.active;
    if (tabbar->data.tabbar.n_tabs == 0) {
        new_active = -1;
    } else if (index == tabbar->data.tabbar.active) {
        if (new_active >= tabbar->data.tabbar.n_tabs) {
            new_active = tabbar->data.tabbar.n_tabs - 1;
        }
        /* The new "current" panel needs to become visible. */
        if (new_active >= 0 && tabbar->data.tabbar.panels[new_active]) {
            yetty_ygui_widget_set_visible(tabbar->data.tabbar.panels[new_active], 1);
        }
    } else if (index < tabbar->data.tabbar.active) {
        new_active--;
    }
    tabbar->data.tabbar.active = new_active;

    if (tabbar->change_callback && new_active >= 0) {
        tabbar->change_callback(tabbar, (float)new_active, tabbar->change_userdata);
    }
    if (tabbar->engine) {
        tabbar->engine->dirty = 1;
        tabbar->dirty = 1;
    }
}

void yetty_ygui_widget_tabbar_on_tab_close(struct yetty_ygui_widget *tabbar,
                                           ygui_change_callback_t callback, void *userdata)
{
    if (!tabbar || tabbar->type != YETTY_YGUI_WIDGET_TABBAR) {
        return;
    }
    tabbar->data.tabbar.on_tab_close = callback;
    tabbar->data.tabbar.on_tab_close_userdata = userdata;
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
        tabbar->dirty = 1;
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

void yetty_ygui_widget_tabbar_on_new_tab(struct yetty_ygui_widget *tabbar,
                                         ygui_click_callback_t callback, void *userdata)
{
    if (!tabbar || tabbar->type != YETTY_YGUI_WIDGET_TABBAR) {
        return;
    }
    tabbar->data.tabbar.on_new_tab = callback;
    tabbar->data.tabbar.on_new_tab_userdata = userdata;
    if (tabbar->engine) {
        tabbar->engine->dirty = 1;
        tabbar->dirty = 1;
    }
}
