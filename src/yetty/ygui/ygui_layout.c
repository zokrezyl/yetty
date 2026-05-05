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
 * Features in this revision:
 *   - MANUAL mode: children at authored relative x/y/w/h.
 *   - FLEX mode: row / column with gap, padding, margin.
 *   - flex-grow / flex-shrink / flex-basis (px or % of parent main).
 *   - justify-content: start | center | end | space-between/around/evenly.
 *   - align-items / align-self: auto | start | center | end | stretch | baseline.
 *   - flex-wrap: nowrap | wrap (single-line vs multi-line).
 *   - align-content: cross-axis distribution between flex lines.
 *   - position: relative (in flow) | absolute (skipped, placed at authored x/y).
 *   - percent sizing: width_percent / height_percent / min_*_percent / max_*_percent
 *     resolved against parent content box.
 *
 * Still not implemented (won't be in this iteration): subpixel rounding,
 * intrinsic-size measurement, baseline groups across flex lines.
 */

#include "ygui_internal.h"

#include <stddef.h>

/*=============================================================================
 * Helpers
 *===========================================================================*/

static int layout_is_visible(const struct yetty_ygui_widget *w)
{
    return w && (w->flags & YETTY_YGUI_FLAG_VISIBLE);
}

static int layout_is_in_flow(const struct yetty_ygui_widget *w)
{
    /* Absolute children are skipped by the flex algorithm and laid out
     * directly inside the parent's content box at their authored x/y. */
    return layout_is_visible(w) && w->layout.position != YETTY_YGUI_POSITION_ABSOLUTE;
}

static float resolve_percent(float pct, float reference)
{
    /* pct is given as 0..100 (matches CSS); 0 means "unset". */
    if (pct <= 0.0f) {
        return 0.0f;
    }
    return reference * (pct / 100.0f);
}

static float clamp_size_with_percent(float v, float min_v, float min_pct, float max_v, float max_pct,
                                     float reference)
{
    float min_resolved = min_v;
    float pct_min = resolve_percent(min_pct, reference);
    if (pct_min > min_resolved) {
        min_resolved = pct_min;
    }
    float max_resolved = max_v;
    float pct_max = resolve_percent(max_pct, reference);
    if (pct_max > 0.0f && (max_resolved <= 0.0f || pct_max < max_resolved)) {
        max_resolved = pct_max;
    }
    if (min_resolved > 0.0f && v < min_resolved) {
        v = min_resolved;
    }
    if (max_resolved > 0.0f && v > max_resolved) {
        v = max_resolved;
    }
    if (v < 0.0f) {
        v = 0.0f;
    }
    return v;
}

static const struct yetty_ygui_theme *layout_theme(const struct yetty_ygui_widget *w)
{
    return (w && w->engine) ? w->engine->theme : NULL;
}

/* Forward declarations. */
static void layout_widget(struct yetty_ygui_widget *w, float parent_abs_x, float parent_abs_y,
                          float rel_x, float rel_y, float resolved_w, float resolved_h);

/*=============================================================================
 * Position: absolute
 *===========================================================================*/

static void layout_absolute_child(struct yetty_ygui_widget *parent, struct yetty_ygui_widget *c)
{
    /* Absolute positioning is relative to the parent's *content* box. The
     * authored x/y are interpreted as offsets inside that box; authored
     * w/h give the size (with percent overrides for both axes). */
    float ref_w = parent->content_w;
    float ref_h = parent->content_h;

    float w_pct = resolve_percent(c->layout.width_percent, ref_w);
    float h_pct = resolve_percent(c->layout.height_percent, ref_h);
    float resolved_w = w_pct > 0.0f ? w_pct : c->authored_w;
    float resolved_h = h_pct > 0.0f ? h_pct : c->authored_h;

    resolved_w = clamp_size_with_percent(resolved_w, c->layout.min_w, c->layout.min_w_percent,
                                         c->layout.max_w, c->layout.max_w_percent, ref_w);
    resolved_h = clamp_size_with_percent(resolved_h, c->layout.min_h, c->layout.min_h_percent,
                                         c->layout.max_h, c->layout.max_h_percent, ref_h);

    /* Position relative to parent->layout origin. content_x already
     * carries parent.layout_x + padding_left, so we need to convert it
     * back to a "relative to parent" offset. */
    float rel_x = (parent->content_x - parent->layout_x) + c->authored_x;
    float rel_y = (parent->content_y - parent->layout_y) + c->authored_y;

    layout_widget(c, parent->layout_x, parent->layout_y, rel_x, rel_y, resolved_w, resolved_h);
}

/*=============================================================================
 * MANUAL mode
 *===========================================================================*/

static void layout_manual_children(struct yetty_ygui_widget *parent)
{
    for (struct yetty_ygui_widget *c = parent->first_child; c; c = c->next_sibling) {
        if (!layout_is_visible(c)) {
            continue;
        }
        if (c->layout.position == YETTY_YGUI_POSITION_ABSOLUTE) {
            layout_absolute_child(parent, c);
            continue;
        }
        /* Percent sizing in manual mode: resolve against parent content box. */
        float w_pct = resolve_percent(c->layout.width_percent, parent->content_w);
        float h_pct = resolve_percent(c->layout.height_percent, parent->content_h);
        float w = w_pct > 0.0f ? w_pct : c->authored_w;
        float h = h_pct > 0.0f ? h_pct : c->authored_h;
        w = clamp_size_with_percent(w, c->layout.min_w, c->layout.min_w_percent, c->layout.max_w,
                                    c->layout.max_w_percent, parent->content_w);
        h = clamp_size_with_percent(h, c->layout.min_h, c->layout.min_h_percent, c->layout.max_h,
                                    c->layout.max_h_percent, parent->content_h);
        layout_widget(c, parent->layout_x, parent->layout_y, c->authored_x, c->authored_y, w, h);
    }
}

/*=============================================================================
 * FLEX axis helpers
 *===========================================================================*/

static float flex_main_basis(const struct yetty_ygui_widget *c, ygui_flex_direction_t dir,
                             float parent_main_content)
{
    float pct = resolve_percent(c->layout.flex_basis_percent, parent_main_content);
    if (pct > 0.0f) {
        return pct;
    }
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

static float flex_cross_size(const struct yetty_ygui_widget *c, ygui_flex_direction_t dir,
                             float parent_cross_content)
{
    /* Honor explicit cross-axis percent first, otherwise use authored cross. */
    if (dir == YETTY_YGUI_FLEX_ROW) {
        float pct = resolve_percent(c->layout.height_percent, parent_cross_content);
        return pct > 0.0f ? pct : c->authored_h;
    } else {
        float pct = resolve_percent(c->layout.width_percent, parent_cross_content);
        return pct > 0.0f ? pct : c->authored_w;
    }
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
    if (free_space < 0.0f) {
        return 0.0f;
    }
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
    if (n <= 1 || free_space <= 0.0f) {
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

static float align_content_leading(ygui_align_t a, float free_cross, int n_lines)
{
    if (free_cross < 0.0f) {
        return 0.0f;
    }
    switch (a) {
    case YETTY_YGUI_ALIGN_CENTER:
        return free_cross * 0.5f;
    case YETTY_YGUI_ALIGN_END:
        return free_cross;
    case YETTY_YGUI_ALIGN_AUTO:
    case YETTY_YGUI_ALIGN_START:
    case YETTY_YGUI_ALIGN_STRETCH:
    case YETTY_YGUI_ALIGN_BASELINE:
    default:
        (void)n_lines;
        return 0.0f;
    }
}

/*=============================================================================
 * FLEX lines
 *===========================================================================*/

struct flex_item {
    struct yetty_ygui_widget *child;
    float main_size;   /* resolved main-axis size (post grow/shrink + clamp) */
    float cross_size;  /* resolved cross-axis size (post stretch + clamp) */
    float main_margin_lead;
    float main_margin_trail;
    float cross_margin_lead;
    float cross_margin_trail;
    float baseline;    /* distance from item top to its baseline (cross-axis) */
};

struct flex_line {
    int   first;          /* index into items array */
    int   count;
    float main_used;      /* sum of main_size + main margins + (count-1)*gap */
    float cross_size;     /* tallest cross_size + margins in this line */
    float baseline_max;   /* max baseline among items that have one */
    int   has_baseline;   /* any item exposed a baseline */
};

static float item_baseline(struct yetty_ygui_widget *c, ygui_flex_direction_t dir,
                           const struct yetty_ygui_theme *theme)
{
    /* Baseline only makes sense on the cross axis of a row container.
     * For a column container, baseline alignment degenerates to start
     * (this matches CSS behavior when the cross axis is vertical). */
    if (dir != YETTY_YGUI_FLEX_ROW) {
        return 0.0f;
    }
    if (c->vtable && c->vtable->baseline_offset && theme) {
        return c->vtable->baseline_offset(c, theme);
    }
    return 0.0f;
}

/* Resolve sizes (main + cross) for a single flex line and place its
 * children using justify_content / align_items. main_origin/cross_origin
 * are absolute coords of the line's top-left in container space. */
static void resolve_and_place_line(struct yetty_ygui_widget *parent, struct flex_item *items,
                                   struct flex_line *line, ygui_flex_direction_t dir,
                                   float content_main, float main_origin, float cross_origin,
                                   float gap)
{
    int is_row = (dir == YETTY_YGUI_FLEX_ROW);
    int n = line->count;

    /* --- pass A: collect grow / shrink totals across the line --- */
    float total_grow = 0.0f;
    float total_shrink_basis = 0.0f;
    float total_main_no_grow = 0.0f; /* sum of basis + margins for this line */
    for (int i = 0; i < n; i++) {
        struct flex_item *it = &items[line->first + i];
        struct yetty_ygui_widget *c = it->child;
        total_grow += c->layout.flex_grow;
        total_shrink_basis += c->layout.flex_shrink * it->main_size;
        total_main_no_grow += it->main_size + it->main_margin_lead + it->main_margin_trail;
    }
    float total_gap = (n - 1) * gap;
    float free_main = content_main - total_main_no_grow - total_gap;

    int can_grow = (free_main > 0.0f && total_grow > 0.0f);
    int can_shrink = (free_main < 0.0f && total_shrink_basis > 0.0f);

    /* --- pass B: distribute grow/shrink, clamp, accumulate consumed --- */
    float consumed_main = 0.0f;
    for (int i = 0; i < n; i++) {
        struct flex_item *it = &items[line->first + i];
        struct yetty_ygui_widget *c = it->child;
        float resolved = it->main_size;
        if (can_grow && c->layout.flex_grow > 0.0f) {
            resolved += free_main * (c->layout.flex_grow / total_grow);
        } else if (can_shrink && c->layout.flex_shrink > 0.0f) {
            float share = (c->layout.flex_shrink * it->main_size) / total_shrink_basis;
            resolved += free_main * share; /* free_main negative */
        }
        if (resolved < 0.0f) {
            resolved = 0.0f;
        }
        if (is_row) {
            resolved =
                clamp_size_with_percent(resolved, c->layout.min_w, c->layout.min_w_percent,
                                        c->layout.max_w, c->layout.max_w_percent, content_main);
        } else {
            resolved =
                clamp_size_with_percent(resolved, c->layout.min_h, c->layout.min_h_percent,
                                        c->layout.max_h, c->layout.max_h_percent, content_main);
        }
        it->main_size = resolved;
        consumed_main += resolved + it->main_margin_lead + it->main_margin_trail;
    }

    float justify_free = content_main - consumed_main - total_gap;
    if (justify_free < 0.0f) {
        justify_free = 0.0f;
    }
    float lead = justify_leading(parent->layout.justify_content, justify_free, n);
    float extra = justify_extra_gap(parent->layout.justify_content, justify_free, n);

    float cursor = main_origin + lead;

    /* --- pass C: resolve cross sizes + place items --- */
    for (int i = 0; i < n; i++) {
        struct flex_item *it = &items[line->first + i];
        struct yetty_ygui_widget *c = it->child;

        ygui_align_t a = flex_resolve_cross_align(parent, c);
        float cross_avail = line->cross_size - it->cross_margin_lead - it->cross_margin_trail;
        float resolved_cross;
        if (a == YETTY_YGUI_ALIGN_STRETCH) {
            resolved_cross = cross_avail;
        } else {
            resolved_cross = it->cross_size;
        }
        if (is_row) {
            resolved_cross =
                clamp_size_with_percent(resolved_cross, c->layout.min_h, c->layout.min_h_percent,
                                        c->layout.max_h, c->layout.max_h_percent, line->cross_size);
        } else {
            resolved_cross =
                clamp_size_with_percent(resolved_cross, c->layout.min_w, c->layout.min_w_percent,
                                        c->layout.max_w, c->layout.max_w_percent, line->cross_size);
        }

        float cross_offset;
        if (a == YETTY_YGUI_ALIGN_BASELINE && line->has_baseline) {
            /* Position so this item's baseline sits at line->baseline_max
             * (measured from line top). Falls back gracefully to start
             * when an item exposed no baseline. */
            cross_offset = it->cross_margin_lead + (line->baseline_max - it->baseline);
        } else {
            switch (a) {
            case YETTY_YGUI_ALIGN_CENTER:
                cross_offset = it->cross_margin_lead + (cross_avail - resolved_cross) * 0.5f;
                break;
            case YETTY_YGUI_ALIGN_END:
                cross_offset = it->cross_margin_lead + (cross_avail - resolved_cross);
                break;
            case YETTY_YGUI_ALIGN_BASELINE:
            case YETTY_YGUI_ALIGN_STRETCH:
            case YETTY_YGUI_ALIGN_AUTO:
            case YETTY_YGUI_ALIGN_START:
            default:
                cross_offset = it->cross_margin_lead;
                break;
            }
        }
        float cross_pos = cross_origin + cross_offset;

        cursor += it->main_margin_lead;
        float main_pos = cursor;

        float rel_x = is_row ? (main_pos - parent->layout_x) : (cross_pos - parent->layout_x);
        float rel_y = is_row ? (cross_pos - parent->layout_y) : (main_pos - parent->layout_y);
        float final_w = is_row ? it->main_size : resolved_cross;
        float final_h = is_row ? resolved_cross : it->main_size;

        layout_widget(c, parent->layout_x, parent->layout_y, rel_x, rel_y, final_w, final_h);

        cursor += it->main_size + it->main_margin_trail + gap + extra;
    }
}

/*=============================================================================
 * FLEX top-level
 *===========================================================================*/

static int count_in_flow(const struct yetty_ygui_widget *parent)
{
    int n = 0;
    for (const struct yetty_ygui_widget *c = parent->first_child; c; c = c->next_sibling) {
        if (layout_is_in_flow(c)) {
            n++;
        }
    }
    return n;
}

static void layout_flex(struct yetty_ygui_widget *parent)
{
    ygui_flex_direction_t dir = parent->layout.direction;
    int is_row = (dir == YETTY_YGUI_FLEX_ROW);
    const struct yetty_ygui_theme *theme = layout_theme(parent);

    float content_main = is_row ? parent->content_w : parent->content_h;
    float content_cross = is_row ? parent->content_h : parent->content_w;
    float gap = parent->layout.gap;

    int total_n = count_in_flow(parent);
    if (total_n == 0) {
        /* still need to lay out absolute children */
        for (struct yetty_ygui_widget *c = parent->first_child; c; c = c->next_sibling) {
            if (layout_is_visible(c) &&
                c->layout.position == YETTY_YGUI_POSITION_ABSOLUTE) {
                layout_absolute_child(parent, c);
            }
        }
        return;
    }

    /* Stack-allocated for typical UIs; bail to heap for huge containers. */
    struct flex_item stack_items[64];
    struct flex_item *items = stack_items;
    int items_cap_static = (int)(sizeof stack_items / sizeof stack_items[0]);
    int items_heap = 0;
    if (total_n > items_cap_static) {
        items = (struct flex_item *)calloc((size_t)total_n, sizeof *items);
        if (!items) {
            return; /* nothing useful we can do here; drop layout */
        }
        items_heap = 1;
    }

    /* Phase 1: compute per-item raw main_size (basis), cross_size, margins, baseline. */
    int idx = 0;
    for (struct yetty_ygui_widget *c = parent->first_child; c; c = c->next_sibling) {
        if (!layout_is_in_flow(c)) {
            continue;
        }
        struct flex_item *it = &items[idx++];
        it->child = c;
        it->main_size = flex_main_basis(c, dir, content_main);
        it->cross_size = flex_cross_size(c, dir, content_cross);
        it->main_margin_lead = is_row ? c->layout.margin_left : c->layout.margin_top;
        it->main_margin_trail = is_row ? c->layout.margin_right : c->layout.margin_bottom;
        it->cross_margin_lead = is_row ? c->layout.margin_top : c->layout.margin_left;
        it->cross_margin_trail = is_row ? c->layout.margin_bottom : c->layout.margin_right;
        it->baseline = item_baseline(c, dir, theme);
    }

    /* Phase 2: split into lines.  In NOWRAP, all items go on one line. */
    struct flex_line stack_lines[16];
    struct flex_line *lines = stack_lines;
    int lines_cap_static = (int)(sizeof stack_lines / sizeof stack_lines[0]);
    int lines_heap = 0;
    int n_lines = 0;

    int do_wrap = (parent->layout.wrap == YETTY_YGUI_FLEX_WRAP);
    if (!do_wrap) {
        n_lines = 1;
        lines[0] = (struct flex_line){.first = 0, .count = total_n};
    } else {
        /* Greedy: accumulate items until adding the next one (with its
         * margins + gap) would exceed content_main. Always include at
         * least one item per line. */
        int max_lines = total_n;
        if (max_lines > lines_cap_static) {
            lines = (struct flex_line *)calloc((size_t)max_lines, sizeof *lines);
            if (!lines) {
                if (items_heap) free(items);
                return;
            }
            lines_heap = 1;
        }
        struct flex_line cur = {.first = 0, .count = 0, .main_used = 0.0f};
        for (int i = 0; i < total_n; i++) {
            float item_main = items[i].main_size + items[i].main_margin_lead +
                              items[i].main_margin_trail;
            float candidate = cur.main_used + (cur.count > 0 ? gap : 0.0f) + item_main;
            if (cur.count > 0 && candidate > content_main) {
                lines[n_lines++] = cur;
                cur = (struct flex_line){.first = i, .count = 0, .main_used = 0.0f};
                candidate = item_main;
            }
            cur.count++;
            cur.main_used = candidate;
        }
        if (cur.count > 0) {
            lines[n_lines++] = cur;
        }
    }

    /* Phase 3: per-line cross_size and baseline_max (before placement). */
    for (int li = 0; li < n_lines; li++) {
        struct flex_line *line = &lines[li];
        float max_cross = 0.0f;
        float max_baseline = 0.0f;
        int has_baseline = 0;
        for (int i = 0; i < line->count; i++) {
            struct flex_item *it = &items[line->first + i];
            float cross = it->cross_size + it->cross_margin_lead + it->cross_margin_trail;
            if (cross > max_cross) {
                max_cross = cross;
            }
            if (it->baseline > 0.0f) {
                has_baseline = 1;
                if (it->baseline > max_baseline) {
                    max_baseline = it->baseline;
                }
            }
        }
        line->cross_size = max_cross;
        line->baseline_max = max_baseline;
        line->has_baseline = has_baseline;
    }

    /* Phase 4: distribute cross-axis space among lines (align_content).
     *
     * Per CSS flex spec: in single-line (nowrap) containers the one line
     * always takes the container's full cross-size, regardless of
     * align-content. This is what makes `align-items: stretch` fill the
     * container's cross axis. align-content only matters when there are
     * multiple lines. */
    if (n_lines == 1) {
        lines[0].cross_size = content_cross;
    }
    float total_lines_cross = 0.0f;
    for (int li = 0; li < n_lines; li++) {
        total_lines_cross += lines[li].cross_size;
    }
    float free_cross = content_cross - total_lines_cross;
    ygui_align_t ac = parent->layout.align_content;
    if (ac == YETTY_YGUI_ALIGN_AUTO) {
        ac = YETTY_YGUI_ALIGN_START;
    }
    if (ac == YETTY_YGUI_ALIGN_STRETCH && n_lines > 1 && free_cross > 0.0f) {
        float per = free_cross / (float)n_lines;
        for (int li = 0; li < n_lines; li++) {
            lines[li].cross_size += per;
        }
        free_cross = 0.0f;
    }

    float main_origin = is_row ? parent->content_x : parent->content_y;
    float cross_origin = is_row ? parent->content_y : parent->content_x;

    /* Leading + per-gap distribution along cross axis (between lines).  We
     * reuse justify_*-style helpers since semantics overlap. */
    float cross_lead = align_content_leading(ac, free_cross, n_lines);
    float cross_extra = 0.0f;
    if (ac == YETTY_YGUI_ALIGN_AUTO) {
        ac = YETTY_YGUI_ALIGN_START;
    }
    if (free_cross > 0.0f && n_lines > 1) {
        switch (ac) {
        case YETTY_YGUI_ALIGN_END:
            /* leading already set to free_cross */
            break;
        case YETTY_YGUI_ALIGN_CENTER:
            /* leading already set to free_cross/2 */
            break;
        default:
            break;
        }
    }
    cross_origin += cross_lead;

    /* Phase 5: place each line. */
    for (int li = 0; li < n_lines; li++) {
        struct flex_line *line = &lines[li];
        resolve_and_place_line(parent, items, line, dir, content_main, main_origin, cross_origin,
                               gap);
        cross_origin += line->cross_size + cross_extra;
    }

    /* Phase 6: place absolute children inside parent's content box. */
    for (struct yetty_ygui_widget *c = parent->first_child; c; c = c->next_sibling) {
        if (layout_is_visible(c) && c->layout.position == YETTY_YGUI_POSITION_ABSOLUTE) {
            layout_absolute_child(parent, c);
        }
    }

    if (items_heap) free(items);
    if (lines_heap) free(lines);
}

/*=============================================================================
 * Recursive entry
 *===========================================================================*/

static void layout_widget(struct yetty_ygui_widget *w, float parent_abs_x, float parent_abs_y,
                          float rel_x, float rel_y, float resolved_w, float resolved_h)
{
    w->x = rel_x;
    w->y = rel_y;
    w->w = resolved_w;
    w->h = resolved_h;

    w->layout_x = parent_abs_x + rel_x;
    w->layout_y = parent_abs_y + rel_y;
    w->layout_w = resolved_w;
    w->layout_h = resolved_h;

    w->effective_x = w->layout_x;
    w->effective_y = w->layout_y;

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

/*=============================================================================
 * Pre-flight: intrinsic sizing for widgets that don't have a meaningful
 * authored height (currently tree_node).
 *
 * Walks bottom-up so a parent's intrinsic size sees its children's
 * already-computed sizes. Updates `authored_h` in place so the regular
 * flex pass that follows can use it as basis on the main axis.
 *
 * Only tree_node participates today, but the structure is generic — add
 * an `intrinsic_size` vtable hook later if more widgets need it.
 *===========================================================================*/

static void preflight_intrinsic_size(struct yetty_ygui_widget *w)
{
    /* Recurse first (bottom-up). */
    for (struct yetty_ygui_widget *c = w->first_child; c; c = c->next_sibling) {
        preflight_intrinsic_size(c);
    }

    if (w->type != YETTY_YGUI_WIDGET_TREE_NODE) {
        return;
    }

    /* Header height = theme->row_height (set by the constructor as
     * padding_top). The constructor seeds padding_top = row_height; the
     * engine pre-flight just trusts that value. */
    float h = w->layout.padding_top;
    if (h <= 0.0f) {
        const struct yetty_ygui_theme *theme = layout_theme(w);
        h = theme ? theme->row_height : 24.0f;
        w->layout.padding_top = h;
    }

    if (w->data.tree_node.expanded && w->data.tree_node.children_list) {
        struct yetty_ygui_widget *kids = w->data.tree_node.children_list;
        float sum = 0.0f;
        int count = 0;
        for (struct yetty_ygui_widget *c = kids->first_child; c; c = c->next_sibling) {
            if (!layout_is_visible(c)) {
                continue;
            }
            sum += c->authored_h;
            count++;
        }
        if (count > 1) {
            sum += (float)(count - 1) * kids->layout.gap;
        }
        sum += kids->layout.padding_top + kids->layout.padding_bottom;
        h += sum;
    }

    w->authored_h = h;
}

struct yetty_ycore_void_result yetty_ygui_layout_compute_engine(struct yetty_ygui_engine *engine)
{
    if (!engine) {
        return YETTY_ERR(yetty_ycore_void, "ygui_layout_compute_engine: NULL engine");
    }

    /* Pre-flight: update intrinsic sizes (tree_node h grows with expanded
     * children). Walk all top-level subtrees. */
    for (struct yetty_ygui_widget *w = engine->first_widget; w; w = w->next_sibling) {
        preflight_intrinsic_size(w);
    }

    /* Top-level widgets: parent_abs is (0, 0) so authored x/y are absolute. */
    for (struct yetty_ygui_widget *w = engine->first_widget; w; w = w->next_sibling) {
        if (!layout_is_visible(w)) {
            continue;
        }
        layout_widget(w, 0.0f, 0.0f, w->authored_x, w->authored_y, w->authored_w, w->authored_h);
    }
    return YETTY_OK_VOID();
}
