/*
 * ygui_layout.c — flexbox-style layout pass.
 *
 * Runs before rendering. Reads each widget's authored_x/y/w/h and the
 * embedded `struct yetty_ygui_layout`, then writes:
 *   - relative live x/y/w/h (consumed by widget render functions)
 *   - absolute layout_x/y/w/h (consumed by the spatial grid)
 *   - content_x/y/w/h (inner box after padding)
 *   - effective_x/y (legacy alias of layout_x/y)
 *
 * MANUAL mode reproduces today's "render at authored x/y" semantics.
 * FLEX mode supports row/column, gap, padding, margin, grow/shrink/basis,
 * justify-content, and align-items/align-self per the issue #41 milestone.
 *
 * Wrapping, baseline alignment, percent sizing, and absolute-positioned
 * children inside flex containers are intentionally not implemented yet.
 */

#include "ygui_internal.h"

static int layout_is_visible(const struct yetty_ygui_widget *w)
{
    return w && (w->flags & YETTY_YGUI_FLAG_VISIBLE);
}

static float layout_clamp_size(float v, float min_v, float max_v)
{
    if (min_v > 0.0f && v < min_v) {
        v = min_v;
    }
    if (max_v > 0.0f && v > max_v) {
        v = max_v;
    }
    if (v < 0.0f) {
        v = 0.0f;
    }
    return v;
}

static void layout_widget(struct yetty_ygui_widget *w, float parent_abs_x, float parent_abs_y,
                          float rel_x, float rel_y, float resolved_w, float resolved_h);

/*=============================================================================
 * MANUAL mode
 *===========================================================================*/

static void layout_manual_children(struct yetty_ygui_widget *parent)
{
    /* Children keep their authored relative coordinates inside parent. */
    for (struct yetty_ygui_widget *c = parent->first_child; c; c = c->next_sibling) {
        if (!layout_is_visible(c)) {
            continue;
        }
        layout_widget(c, parent->layout_x, parent->layout_y, c->authored_x, c->authored_y,
                      c->authored_w, c->authored_h);
    }
}

/*=============================================================================
 * FLEX mode
 *===========================================================================*/

static float flex_main_basis(const struct yetty_ygui_widget *c, ygui_flex_direction_t dir)
{
    if (c->layout.flex_basis > 0.0f) {
        return c->layout.flex_basis;
    }
    return (dir == YETTY_YGUI_FLEX_ROW) ? c->authored_w : c->authored_h;
}

static float flex_main_margin(const struct yetty_ygui_widget *c, ygui_flex_direction_t dir)
{
    return (dir == YETTY_YGUI_FLEX_ROW) ? (c->layout.margin_left + c->layout.margin_right)
                                        : (c->layout.margin_top + c->layout.margin_bottom);
}

static float flex_cross_size(const struct yetty_ygui_widget *c, ygui_flex_direction_t dir)
{
    return (dir == YETTY_YGUI_FLEX_ROW) ? c->authored_h : c->authored_w;
}

static ygui_align_t flex_resolve_cross_align(const struct yetty_ygui_widget *parent,
                                             const struct yetty_ygui_widget *c)
{
    ygui_align_t a = c->layout.align_self;
    if (a == YETTY_YGUI_ALIGN_AUTO) {
        a = parent->layout.align_items;
    }
    if (a == YETTY_YGUI_ALIGN_AUTO) {
        a = YETTY_YGUI_ALIGN_START;
    }
    return a;
}

static float justify_leading(ygui_justify_t j, float free_space, int n)
{
    switch (j) {
    case YETTY_YGUI_JUSTIFY_CENTER:
        return free_space * 0.5f;
    case YETTY_YGUI_JUSTIFY_END:
        return free_space;
    case YETTY_YGUI_JUSTIFY_SPACE_AROUND:
        return n > 0 ? (free_space / (float)(2 * n)) : 0.0f;
    case YETTY_YGUI_JUSTIFY_SPACE_EVENLY:
        return n >= 0 ? (free_space / (float)(n + 1)) : 0.0f;
    case YETTY_YGUI_JUSTIFY_START:
    case YETTY_YGUI_JUSTIFY_SPACE_BETWEEN:
    default:
        return 0.0f;
    }
}

static float justify_extra_gap(ygui_justify_t j, float free_space, int n)
{
    if (n <= 1) {
        return 0.0f;
    }
    switch (j) {
    case YETTY_YGUI_JUSTIFY_SPACE_BETWEEN:
        return free_space / (float)(n - 1);
    case YETTY_YGUI_JUSTIFY_SPACE_AROUND:
        return free_space / (float)n;
    case YETTY_YGUI_JUSTIFY_SPACE_EVENLY:
        return free_space / (float)(n + 1);
    default:
        return 0.0f;
    }
}

static void layout_flex(struct yetty_ygui_widget *c)
{
    ygui_flex_direction_t dir = c->layout.direction;
    int is_row = (dir == YETTY_YGUI_FLEX_ROW);

    float content_main = is_row ? c->content_w : c->content_h;
    float content_cross = is_row ? c->content_h : c->content_w;

    /* Pass 1: count visible children, sum bases + margins on main axis. */
    int n = 0;
    float total_main = 0.0f;
    float total_grow = 0.0f;
    float total_shrink_basis = 0.0f;
    for (struct yetty_ygui_widget *k = c->first_child; k; k = k->next_sibling) {
        if (!layout_is_visible(k)) {
            continue;
        }
        n++;
        float basis = flex_main_basis(k, dir);
        total_main += basis + flex_main_margin(k, dir);
        total_grow += k->layout.flex_grow;
        total_shrink_basis += k->layout.flex_shrink * basis;
    }

    if (n == 0) {
        return;
    }

    float gap = c->layout.gap;
    float total_gap = (n - 1) * gap;
    float free_space = content_main - total_main - total_gap;

    /* Pass 2: distribute free space via grow or shrink. */
    float distribute = free_space;
    int can_grow = (free_space > 0.0f && total_grow > 0.0f);
    int can_shrink = (free_space < 0.0f && total_shrink_basis > 0.0f);

    /* Pre-compute resolved main sizes and remaining slack-for-justify. */
    float consumed_main = 0.0f;
    for (struct yetty_ygui_widget *k = c->first_child; k; k = k->next_sibling) {
        if (!layout_is_visible(k)) {
            continue;
        }
        float basis = flex_main_basis(k, dir);
        float resolved = basis;
        if (can_grow && k->layout.flex_grow > 0.0f) {
            resolved += distribute * (k->layout.flex_grow / total_grow);
        } else if (can_shrink && k->layout.flex_shrink > 0.0f) {
            float share = (k->layout.flex_shrink * basis) / total_shrink_basis;
            resolved += distribute * share; /* distribute is negative */
        }
        if (resolved < 0.0f) {
            resolved = 0.0f;
        }
        if (is_row) {
            resolved = layout_clamp_size(resolved, k->layout.min_w, k->layout.max_w);
        } else {
            resolved = layout_clamp_size(resolved, k->layout.min_h, k->layout.max_h);
        }
        /* Stash resolved main size in the live w/h for now; the second walk
         * will read it back. */
        if (is_row) {
            k->w = resolved;
        } else {
            k->h = resolved;
        }
        consumed_main += resolved + flex_main_margin(k, dir);
    }

    float justify_free = content_main - consumed_main - total_gap;
    if (justify_free < 0.0f) {
        justify_free = 0.0f;
    }
    float lead = justify_leading(c->layout.justify_content, justify_free, n);
    float extra = justify_extra_gap(c->layout.justify_content, justify_free, n);

    float main_origin = is_row ? c->content_x : c->content_y;
    float cross_origin = is_row ? c->content_y : c->content_x;
    float cursor = main_origin + lead;

    /* Pass 3: place each child. */
    for (struct yetty_ygui_widget *k = c->first_child; k; k = k->next_sibling) {
        if (!layout_is_visible(k)) {
            continue;
        }
        float main_margin_lead = is_row ? k->layout.margin_left : k->layout.margin_top;
        float main_margin_trail = is_row ? k->layout.margin_right : k->layout.margin_bottom;
        float cross_margin_lead = is_row ? k->layout.margin_top : k->layout.margin_left;
        float cross_margin_trail = is_row ? k->layout.margin_bottom : k->layout.margin_right;

        float resolved_main = is_row ? k->w : k->h;

        /* Cross-axis size and offset. */
        ygui_align_t a = flex_resolve_cross_align(c, k);
        float cross_avail = content_cross - cross_margin_lead - cross_margin_trail;
        float resolved_cross;
        if (a == YETTY_YGUI_ALIGN_STRETCH) {
            resolved_cross = cross_avail;
        } else {
            resolved_cross = flex_cross_size(k, dir);
        }
        if (is_row) {
            resolved_cross = layout_clamp_size(resolved_cross, k->layout.min_h, k->layout.max_h);
        } else {
            resolved_cross = layout_clamp_size(resolved_cross, k->layout.min_w, k->layout.max_w);
        }

        float cross_offset;
        switch (a) {
        case YETTY_YGUI_ALIGN_CENTER:
            cross_offset = cross_margin_lead + (cross_avail - resolved_cross) * 0.5f;
            break;
        case YETTY_YGUI_ALIGN_END:
            cross_offset = cross_margin_lead + (cross_avail - resolved_cross);
            break;
        case YETTY_YGUI_ALIGN_STRETCH:
        case YETTY_YGUI_ALIGN_START:
        default:
            cross_offset = cross_margin_lead;
            break;
        }
        float cross_pos = cross_origin + cross_offset;

        cursor += main_margin_lead;
        float main_pos = cursor;

        /* Resolve relative-to-parent (rel_x, rel_y). */
        float rel_x = is_row ? (main_pos - c->layout_x) : (cross_pos - c->layout_x);
        float rel_y = is_row ? (cross_pos - c->layout_y) : (main_pos - c->layout_y);
        float final_w = is_row ? resolved_main : resolved_cross;
        float final_h = is_row ? resolved_cross : resolved_main;

        layout_widget(k, c->layout_x, c->layout_y, rel_x, rel_y, final_w, final_h);

        cursor += resolved_main + main_margin_trail + gap + extra;
    }
}

/*=============================================================================
 * Recursive entry
 *===========================================================================*/

static void layout_widget(struct yetty_ygui_widget *w, float parent_abs_x, float parent_abs_y,
                          float rel_x, float rel_y, float resolved_w, float resolved_h)
{
    /* Live geometry — relative to immediate parent (or absolute when called
     * for a top-level widget with parent_abs_* == 0). */
    w->x = rel_x;
    w->y = rel_y;
    w->w = resolved_w;
    w->h = resolved_h;

    /* Absolute resolved box. */
    w->layout_x = parent_abs_x + rel_x;
    w->layout_y = parent_abs_y + rel_y;
    w->layout_w = resolved_w;
    w->layout_h = resolved_h;

    /* Legacy alias kept for existing render functions / external readers. */
    w->effective_x = w->layout_x;
    w->effective_y = w->layout_y;

    /* Inner content box after padding. */
    float pad_l = w->layout.padding_left;
    float pad_r = w->layout.padding_right;
    float pad_t = w->layout.padding_top;
    float pad_b = w->layout.padding_bottom;
    w->content_x = w->layout_x + pad_l;
    w->content_y = w->layout_y + pad_t;
    w->content_w = resolved_w - pad_l - pad_r;
    w->content_h = resolved_h - pad_t - pad_b;
    if (w->content_w < 0.0f) {
        w->content_w = 0.0f;
    }
    if (w->content_h < 0.0f) {
        w->content_h = 0.0f;
    }

    if (!w->first_child) {
        return;
    }

    if (w->layout.mode == YETTY_YGUI_LAYOUT_FLEX) {
        layout_flex(w);
    } else {
        layout_manual_children(w);
    }
}

struct yetty_ycore_void_result yetty_ygui_layout_compute_engine(struct yetty_ygui_engine *engine)
{
    if (!engine) {
        return YETTY_ERR(yetty_ycore_void, "ygui_layout_compute_engine: NULL engine");
    }

    /* Top-level widgets: their authored x/y are absolute screen coordinates
     * because parent_abs_* is zero. */
    for (struct yetty_ygui_widget *w = engine->first_widget; w; w = w->next_sibling) {
        if (!layout_is_visible(w)) {
            continue;
        }
        layout_widget(w, 0.0f, 0.0f, w->authored_x, w->authored_y, w->authored_w, w->authored_h);
    }
    return YETTY_OK_VOID();
}
