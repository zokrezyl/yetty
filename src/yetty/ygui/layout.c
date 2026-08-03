/*
 * ygui-layout.c — flex layout pass for the new toolkit.
 *
 * This is a compact but genuinely CSS-flexbox-shaped engine (not the earlier
 * "single-line only" reduction). Per container it:
 *
 *   1. Computes the content box (rect minus padding).
 *   2. Resolves each in-flow child's flex base + cross size, its per-side
 *      margins, and its align-self.
 *   3. Breaks children into lines: one line for wrap == NOWRAP, or as many as
 *      needed for wrap == WRAP (a child that would overflow the main axis
 *      starts a new line).
 *   4. Per line, resolves flexible main sizes with the CSS "resolve flexible
 *      lengths" freeze loop — grow distributes spare space by flex_grow,
 *      shrink removes overflow weighted by flex_shrink * flex_basis, and a
 *      child that hits its min/max is frozen and the remaining space is
 *      redistributed to its still-flexible siblings.
 *   5. Positions each line's children on the main axis by justify_content and
 *      on the cross axis by align-self (falling back to the parent's align),
 *      honouring margins and the container's scroll offset.
 *   6. Stacks lines on the cross axis (wrap), then recurses into each child.
 *
 * Supported: flex-direction (row/column), justify (start/center/end/
 * space-between), align + per-child align-self (start/center/end/stretch),
 * flex-grow, flex-shrink (basis-weighted), min/max with redistribution,
 * per-side margins, gap, wrap, absolute positioning (a child with
 * layout.absolute is placed at pos_x/pos_y in the content box and skips flex),
 * and an intrinsic content-measure pass that sizes non-grow containers from
 * their children.
 *
 * Still intentionally out of scope: percent sizing (px only), baseline
 * alignment, align-content (wrap lines pack toward the cross start), and
 * writing-mode/RTL (main/cross leading = top/left).
 */

#include "internal.h"

#include "yetty/gen/impl/ygui/widget.h"

#include <stddef.h>
#include <stdlib.h>

static float clamp_size(float v, float min_v, float max_v)
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

static int direction_is_row(const struct yetty_ygui_layout *l)
{
    return l->direction == YETTY_YGUI_FLEX_ROW;
}

/* Forward decl. */
static struct yetty_ycore_void_result layout_node(struct yetty_yclass_object *node,
                                                  struct yetty_ycore_rectangle rect);

/* Intrinsic content size of `node` — used to give a non-grow container a
 * sensible preferred main-axis size when its own width/height is unset
 * (-1). A leaf (no in-flow children) measures 0 on an unset axis; a
 * container measures padding + the sum of its visible in-flow children's
 * sizes along its own direction (+ gaps + the children's main-axis margins),
 * and the max child size (+ cross margins) on the cross axis. Recurses so
 * nested vbox/tree_node stacks size bottom-up.
 *
 * Authored sizes (>= 0) always win; only unset axes are derived. */
static struct yetty_ycore_void_result measure_size(struct yetty_yclass_object *node, float *out_w,
                                                   float *out_h)
{
    struct yetty_ygui_layout_const_ptr_result layout_res = yetty_ygui_widget_layout_get(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, layout_res, "measure_size: layout_get");
    const struct yetty_ygui_layout *l = layout_res.value;
    float w = l->width;
    float h = l->height;
    if (w < 0.0f || h < 0.0f) {
        /* Derive the unset axis from content + padding. Even a childless
         * node contributes its padding (e.g. a tree_node whose header
         * lives in padding_top must measure to the header height). */
        int row = direction_is_row(l);
        float main_sum = 0.0f;
        float cross_max = 0.0f;
        int n = 0;
        struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(node);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, child_res, "measure_size: first_child");
        for (struct yetty_yclass_object *c = child_res.value; c;) {
            struct yetty_ygui_layout_const_ptr_result child_layout_res =
                yetty_ygui_widget_layout_get(c);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, child_layout_res,
                                "measure_size: child layout_get");
            const struct yetty_ygui_layout *cl = child_layout_res.value;
            if (!cl->hidden && !cl->absolute) {
                float cw = 0.0f, ch = 0.0f;
                struct yetty_ycore_void_result m_res = measure_size(c, &cw, &ch);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, m_res, "measure_size: child measure");
                float margin_w = cl->margin_left + cl->margin_right;
                float margin_h = cl->margin_top + cl->margin_bottom;
                float cmain = (row ? cw + margin_w : ch + margin_h);
                float ccross = (row ? ch + margin_h : cw + margin_w);
                main_sum += cmain;
                if (ccross > cross_max) {
                    cross_max = ccross;
                }
                n++;
            }
            struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, next_res, "measure_size: next_sibling");
            c = next_res.value;
        }
        if (n > 1) {
            main_sum += l->gap * (float)(n - 1);
        }
        float content_w = row ? main_sum : cross_max;
        float content_h = row ? cross_max : main_sum;
        if (w < 0.0f) {
            w = content_w + l->padding_left + l->padding_right;
        }
        if (h < 0.0f) {
            h = content_h + l->padding_top + l->padding_bottom;
        }
    }
    if (w < 0.0f) {
        w = 0.0f;
    }
    if (h < 0.0f) {
        h = 0.0f;
    }
    *out_w = clamp_size(w, l->min_width, l->max_width);
    *out_h = clamp_size(h, l->min_height, l->max_height);
    return YETTY_OK_VOID();
}

/* One in-flow child resolved into the parent's main/cross frame. Margins and
 * align-self are pre-projected onto the main/cross axes so the placement code
 * is direction-agnostic. `main_size`/`cross_size` are filled by the sizing
 * pass. */
struct flex_item {
    struct yetty_yclass_object *child;
    float base_main;
    float main_min;
    float main_max;
    float cross_base; /* authored cross size, 0 = auto */
    float cross_min;
    float cross_max;
    float grow;
    float shrink;
    float margin_main_lead;
    float margin_main_trail;
    float margin_cross_lead;
    float margin_cross_trail;
    enum yetty_ygui_flex_align_self align_self;
    float main_size;
    float cross_size;
};

YETTY_YRESULT_DECLARE(flex_item, struct flex_item);

static struct flex_item_result resolve_flex_item(const struct yetty_ygui_layout *parent_layout,
                                                 struct yetty_yclass_object *child)
{
    struct yetty_ygui_layout_const_ptr_result child_layout_res =
        yetty_ygui_widget_layout_get(child);
    YETTY_RETURN_IF_ERR(flex_item, child_layout_res, "resolve_flex_item: layout_get");
    const struct yetty_ygui_layout *cl = child_layout_res.value;
    struct flex_item item = {0};
    item.child = child;
    int row = direction_is_row(parent_layout);
    float authored_main = row ? cl->width : cl->height;
    if (row) {
        item.base_main = cl->width >= 0.0f ? cl->width : 0.0f;
        item.cross_base = cl->height >= 0.0f ? cl->height : 0.0f;
        item.main_min = cl->min_width;
        item.main_max = cl->max_width;
        item.cross_min = cl->min_height;
        item.cross_max = cl->max_height;
        item.margin_main_lead = cl->margin_left;
        item.margin_main_trail = cl->margin_right;
        item.margin_cross_lead = cl->margin_top;
        item.margin_cross_trail = cl->margin_bottom;
    } else {
        item.base_main = cl->height >= 0.0f ? cl->height : 0.0f;
        item.cross_base = cl->width >= 0.0f ? cl->width : 0.0f;
        item.main_min = cl->min_height;
        item.main_max = cl->max_height;
        item.cross_min = cl->min_width;
        item.cross_max = cl->max_width;
        item.margin_main_lead = cl->margin_top;
        item.margin_main_trail = cl->margin_bottom;
        item.margin_cross_lead = cl->margin_left;
        item.margin_cross_trail = cl->margin_right;
    }
    item.grow = cl->flex_grow;
    item.shrink = cl->flex_shrink;
    item.align_self = cl->align_self;
    /* When the child doesn't author its main size and won't grow to fill,
     * derive an intrinsic content size so nested containers (tree_node,
     * vbox-of-rows) stack instead of collapsing to a zero rect. Grow children
     * keep a 0 base — they expand to fill regardless. */
    if (authored_main < 0.0f && item.grow <= 0.0f) {
        float mw = 0.0f, mh = 0.0f;
        struct yetty_ycore_void_result m_res = measure_size(child, &mw, &mh);
        YETTY_RETURN_IF_ERR(flex_item, m_res, "resolve_flex_item: measure_size");
        item.base_main = row ? mw : mh;
    }
    item.base_main = clamp_size(item.base_main, item.main_min, item.main_max);
    return YETTY_OK(flex_item, item);
}

/* CSS "resolve flexible lengths" for one line: fill each item's `main_size`,
 * distributing `content_main` minus the line's margins and gaps by flex_grow
 * (spare space) or flex_shrink * flex_basis (overflow). A child clamped to its
 * min/max is frozen and its surplus/deficit flows to the still-flexible
 * siblings on the next pass, so min/max no longer silently strand space. */
static void resolve_line_main(struct flex_item *items, int start, int count, float content_main,
                              float gap)
{
    if (count <= 0) {
        return;
    }
    float sum_base = 0.0f;
    float sum_margin = 0.0f;
    for (int i = 0; i < count; ++i) {
        struct flex_item *it = &items[start + i];
        sum_base += it->base_main;
        sum_margin += it->margin_main_lead + it->margin_main_trail;
        it->main_size = it->base_main; /* frozen items keep this */
    }
    float total_gap = count > 1 ? gap * (float)(count - 1) : 0.0f;
    float space = content_main - sum_margin - total_gap;
    int growing = (space - sum_base) >= 0.0f;

    /* frozen[] lives in main_size once frozen; track with a parallel flag via
     * a small heap array only when needed. count is tiny (siblings), so a
     * stack VLA-free bounded loop over a calloc'd flag array is simplest. */
    int *frozen = calloc((size_t)count, sizeof(*frozen));
    if (!frozen) {
        /* Out of memory on a per-line flag array: fall back to unflexed base
         * sizes (already stored in main_size) rather than fail the layout. */
        return;
    }
    for (int i = 0; i < count; ++i) {
        struct flex_item *it = &items[start + i];
        float factor = growing ? it->grow : it->shrink;
        if (factor <= 0.0f) {
            frozen[i] = 1;
            it->main_size = clamp_size(it->base_main, it->main_min, it->main_max);
        }
    }
    for (int pass = 0; pass < count; ++pass) {
        float used = 0.0f;
        float sum_factor = 0.0f;
        for (int i = 0; i < count; ++i) {
            struct flex_item *it = &items[start + i];
            if (frozen[i]) {
                used += it->main_size;
            } else {
                used += it->base_main;
                sum_factor += growing ? it->grow : it->shrink * it->base_main;
            }
        }
        if (sum_factor <= 0.0f) {
            break;
        }
        float free_main = space - used;
        int newly_frozen = 0;
        for (int i = 0; i < count; ++i) {
            struct flex_item *it = &items[start + i];
            if (frozen[i]) {
                continue;
            }
            float weight = growing ? it->grow : it->shrink * it->base_main;
            float target = it->base_main + free_main * (weight / sum_factor);
            float clamped = clamp_size(target, it->main_min, it->main_max);
            it->main_size = clamped;
            if (clamped != target) {
                frozen[i] = 1;
                newly_frozen = 1;
            }
        }
        if (!newly_frozen) {
            break;
        }
    }
    free(frozen);
}

/* Cross size of an item within a line of extent `line_cross`, honouring
 * align-self (falling back to the parent align) and the item's cross margins.
 * Only an auto (unauthored) cross size stretches. */
static float item_cross_size(const struct flex_item *it, enum yetty_ygui_flex_align parent_align,
                             float line_cross)
{
    enum yetty_ygui_flex_align eff = parent_align;
    switch (it->align_self) {
    case YETTY_YGUI_ALIGN_SELF_START:
        eff = YETTY_YGUI_ALIGN_START;
        break;
    case YETTY_YGUI_ALIGN_SELF_CENTER:
        eff = YETTY_YGUI_ALIGN_CENTER;
        break;
    case YETTY_YGUI_ALIGN_SELF_END:
        eff = YETTY_YGUI_ALIGN_END;
        break;
    case YETTY_YGUI_ALIGN_SELF_STRETCH:
        eff = YETTY_YGUI_ALIGN_STRETCH;
        break;
    case YETTY_YGUI_ALIGN_SELF_AUTO:
    default:
        break;
    }
    float cross;
    if (eff == YETTY_YGUI_ALIGN_STRETCH && it->cross_base == 0.0f) {
        cross = line_cross - it->margin_cross_lead - it->margin_cross_trail;
    } else {
        cross = it->cross_base;
    }
    return clamp_size(cross, it->cross_min, it->cross_max);
}

/* Cross-axis offset of an item's border box inside a line of extent
 * `line_cross` starting at `line_start` (already inside the content box). */
static float item_cross_offset(const struct flex_item *it, enum yetty_ygui_flex_align parent_align,
                               float line_start, float line_cross, float cross_size)
{
    enum yetty_ygui_flex_align eff = parent_align;
    switch (it->align_self) {
    case YETTY_YGUI_ALIGN_SELF_START:
        eff = YETTY_YGUI_ALIGN_START;
        break;
    case YETTY_YGUI_ALIGN_SELF_CENTER:
        eff = YETTY_YGUI_ALIGN_CENTER;
        break;
    case YETTY_YGUI_ALIGN_SELF_END:
        eff = YETTY_YGUI_ALIGN_END;
        break;
    case YETTY_YGUI_ALIGN_SELF_STRETCH:
        eff = YETTY_YGUI_ALIGN_STRETCH;
        break;
    case YETTY_YGUI_ALIGN_SELF_AUTO:
    default:
        break;
    }
    switch (eff) {
    case YETTY_YGUI_ALIGN_CENTER:
        /* Center the border box within the margin box: consume the leading
         * margin, then split the remaining cross space. Reduces to the plain
         * midpoint when both cross margins are 0. */
        return line_start + it->margin_cross_lead +
               (line_cross - it->margin_cross_lead - cross_size - it->margin_cross_trail) * 0.5f;
    case YETTY_YGUI_ALIGN_END:
        return line_start + line_cross - cross_size - it->margin_cross_trail;
    case YETTY_YGUI_ALIGN_STRETCH:
    case YETTY_YGUI_ALIGN_START:
    default:
        return line_start + it->margin_cross_lead;
    }
}

/* Place one resolved line's children. `line_cross_start` is the cross-axis
 * origin of the line inside the content box; `line_cross` its extent. Recurses
 * into each child. */
static struct yetty_ycore_void_result place_line(struct flex_item *items, int start, int count,
                                                 const struct yetty_ygui_layout *pl,
                                                 float content_min_x, float content_min_y,
                                                 float content_main, float gap, float scroll,
                                                 float line_cross_start, float line_cross)
{
    int row = direction_is_row(pl);
    float sum_main = 0.0f;
    float sum_margin = 0.0f;
    for (int i = 0; i < count; ++i) {
        sum_main += items[start + i].main_size;
        sum_margin += items[start + i].margin_main_lead + items[start + i].margin_main_trail;
    }
    float total_gap = count > 1 ? gap * (float)(count - 1) : 0.0f;
    float leftover = content_main - sum_main - sum_margin - total_gap;

    float main_offset = 0.0f;
    float gap_between = gap;
    if (leftover > 0.0f) {
        switch (pl->justify) {
        case YETTY_YGUI_JUSTIFY_CENTER:
            main_offset = leftover * 0.5f;
            break;
        case YETTY_YGUI_JUSTIFY_END:
            main_offset = leftover;
            break;
        case YETTY_YGUI_JUSTIFY_SPACE_BETWEEN:
            if (count > 1) {
                gap_between += leftover / (float)(count - 1);
            }
            break;
        case YETTY_YGUI_JUSTIFY_START:
        default:
            break;
        }
    }

    float cursor_main = main_offset - scroll;
    for (int i = 0; i < count; ++i) {
        struct flex_item *it = &items[start + i];
        float cross_size = item_cross_size(it, pl->align, line_cross);
        float cross_off =
            item_cross_offset(it, pl->align, line_cross_start, line_cross, cross_size);
        cursor_main += it->margin_main_lead;

        struct yetty_ycore_rectangle crect;
        if (row) {
            crect.min.x = content_min_x + cursor_main;
            crect.min.y = content_min_y + cross_off;
            crect.max.x = crect.min.x + it->main_size;
            crect.max.y = crect.min.y + cross_size;
        } else {
            crect.min.x = content_min_x + cross_off;
            crect.min.y = content_min_y + cursor_main;
            crect.max.x = crect.min.x + cross_size;
            crect.max.y = crect.min.y + it->main_size;
        }
        struct yetty_ycore_void_result r = layout_node(it->child, crect);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "place_line: child");

        cursor_main += it->main_size + it->margin_main_trail + gap_between;
    }
    return YETTY_OK_VOID();
}

/* Resolve, cross-size and place one wrap line [start, start+count), returning
 * the line's cross extent so the caller can advance to the next line. */
static struct yetty_ycore_void_result flush_wrap_line(struct flex_item *items, int start, int count,
                                                      const struct yetty_ygui_layout *pl,
                                                      float content_min_x, float content_min_y,
                                                      float content_main, float scroll,
                                                      float cross_cursor, float *out_line_cross)
{
    resolve_line_main(items, start, count, content_main, pl->gap);
    float line_cross = 0.0f;
    for (int k = start; k < start + count; ++k) {
        float ch = item_cross_size(&items[k], pl->align, 0.0f) + items[k].margin_cross_lead +
                   items[k].margin_cross_trail;
        if (ch > line_cross) {
            line_cross = ch;
        }
    }
    *out_line_cross = line_cross;
    return place_line(items, start, count, pl, content_min_x, content_min_y, content_main, pl->gap,
                      scroll, cross_cursor, line_cross);
}

static struct yetty_ycore_void_result layout_node(struct yetty_yclass_object *node,
                                                  struct yetty_ycore_rectangle rect)
{
    /* Set this node's own rect first — the engine uses it during emit. */
    struct yetty_ycore_void_result sr = yetty_ygui_widget_set_rect(node, rect);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sr, "layout_node: set_rect");

    struct yetty_yclass_object_ptr_result first_child_res = yetty_ygui_widget_first_child(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, first_child_res, "layout_node: first_child");
    if (!first_child_res.value) {
        return YETTY_OK_VOID();
    }

    struct yetty_ygui_layout_const_ptr_result parent_layout_res =
        yetty_ygui_widget_layout_get(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, parent_layout_res, "layout_node: layout_get");
    const struct yetty_ygui_layout *pl = parent_layout_res.value;

    /* Content box = rect minus padding. Always absolute — a scrolling
     * figure renders its (absolute-coord) children clipped by its scissor;
     * no per-node coordinate special-casing here. */
    float content_min_x = rect.min.x + pl->padding_left;
    float content_min_y = rect.min.y + pl->padding_top;
    float content_max_x = rect.max.x - pl->padding_right;
    float content_max_y = rect.max.y - pl->padding_bottom;
    if (content_max_x < content_min_x) {
        content_max_x = content_min_x;
    }
    if (content_max_y < content_min_y) {
        content_max_y = content_min_y;
    }

    float content_w = content_max_x - content_min_x;
    float content_h = content_max_y - content_min_y;
    int row = direction_is_row(pl);
    float content_main = row ? content_w : content_h;
    float content_cross = row ? content_h : content_w;

    struct yetty_ycore_float_result scroll_res = yetty_ygui_widget_scroll_main_get(node);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, scroll_res, "layout_node: scroll_main_get");
    float scroll = scroll_res.value;

    /* Count children so the item array is sized once; also place absolute
     * children (they skip flex bookkeeping entirely). */
    int child_total = 0;
    struct yetty_ycore_void_result loop_r = YETTY_OK_VOID();
    {
        struct yetty_yclass_object_ptr_result iter_res = yetty_ygui_widget_first_child(node);
        YETTY_RETURN_IF_ERR(yetty_ycore_void, iter_res, "layout_node: count first_child");
        for (struct yetty_yclass_object *c = iter_res.value; c;) {
            struct yetty_ygui_layout_const_ptr_result child_layout_res =
                yetty_ygui_widget_layout_get(c);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, child_layout_res,
                                "layout_node: count layout_get");
            const struct yetty_ygui_layout *cl = child_layout_res.value;
            /* Order matters: a hidden child is skipped entirely — no rect, no
             * recurse — even when it is also absolute, so a folded-away
             * positioned overlay keeps its stale geometry and never lays out
             * its subtree. (Checking absolute first would place hidden+absolute
             * children, the regression this order avoids.) */
            if (cl->hidden) {
                /* folded away */
            } else if (cl->absolute) {
                struct yetty_ycore_rectangle crect;
                float aw = cl->width >= 0.0f ? cl->width : 0.0f;
                float ah = cl->height >= 0.0f ? cl->height : 0.0f;
                crect.min.x = content_min_x + cl->pos_x;
                crect.min.y = content_min_y + cl->pos_y;
                crect.max.x = crect.min.x + aw;
                crect.max.y = crect.min.y + ah;
                struct yetty_ycore_void_result r = layout_node(c, crect);
                YETTY_RETURN_IF_ERR(yetty_ycore_void, r, "layout_node: absolute child");
            } else {
                child_total++;
            }
            struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
            YETTY_RETURN_IF_ERR(yetty_ycore_void, next_res, "layout_node: count next_sibling");
            c = next_res.value;
        }
    }
    if (child_total == 0) {
        return YETTY_OK_VOID();
    }

    struct flex_item *items = calloc((size_t)child_total, sizeof(*items));
    if (!items) {
        return YETTY_ERR(yetty_ycore_void, "layout_node: alloc items");
    }

    int item_count = 0;
    {
        struct yetty_yclass_object_ptr_result iter_res = yetty_ygui_widget_first_child(node);
        if (YETTY_IS_ERR(iter_res)) {
            loop_r = YETTY_ERR(yetty_ycore_void, "layout_node: resolve first_child", iter_res);
            goto cleanup;
        }
        for (struct yetty_yclass_object *c = iter_res.value; c && item_count < child_total;) {
            struct yetty_ygui_layout_const_ptr_result child_layout_res =
                yetty_ygui_widget_layout_get(c);
            if (YETTY_IS_ERR(child_layout_res)) {
                loop_r = YETTY_ERR(yetty_ycore_void, "layout_node: resolve layout_get",
                                   child_layout_res);
                goto cleanup;
            }
            const struct yetty_ygui_layout *cl = child_layout_res.value;
            if (!cl->hidden && !cl->absolute) {
                struct flex_item_result item_res = resolve_flex_item(pl, c);
                if (YETTY_IS_ERR(item_res)) {
                    loop_r =
                        YETTY_ERR(yetty_ycore_void, "layout_node: resolve_flex_item", item_res);
                    goto cleanup;
                }
                items[item_count++] = item_res.value;
            }
            struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
            if (YETTY_IS_ERR(next_res)) {
                loop_r = YETTY_ERR(yetty_ycore_void, "layout_node: resolve next_sibling", next_res);
                goto cleanup;
            }
            c = next_res.value;
        }
    }

    /* Break into lines. NOWRAP is a single line of all items. WRAP starts a new
     * line whenever the next item's base + its margins (+ a gap) would overflow
     * the main axis; a single over-large item still occupies its own line. */
    float cross_cursor = 0.0f;
    if (pl->wrap == YETTY_YGUI_WRAP_NOWRAP) {
        resolve_line_main(items, 0, item_count, content_main, pl->gap);
        loop_r = place_line(items, 0, item_count, pl, content_min_x, content_min_y, content_main,
                            pl->gap, scroll, 0.0f, content_cross);
        if (YETTY_IS_ERR(loop_r)) {
            goto cleanup;
        }
    } else {
        int line_start = 0;
        float line_main = 0.0f;
        int on_line = 0;
        for (int i = 0; i < item_count; ++i) {
            float need =
                items[i].base_main + items[i].margin_main_lead + items[i].margin_main_trail;
            float with_gap = need + (on_line > 0 ? pl->gap : 0.0f);
            /* A child that would overflow the current line (and isn't the only
             * one on it) flushes the line first, then starts the next one. */
            if (on_line > 0 && line_main + with_gap > content_main) {
                float line_cross = 0.0f;
                loop_r =
                    flush_wrap_line(items, line_start, i - line_start, pl, content_min_x,
                                    content_min_y, content_main, scroll, cross_cursor, &line_cross);
                if (YETTY_IS_ERR(loop_r)) {
                    goto cleanup;
                }
                cross_cursor += line_cross + pl->gap;
                line_start = i;
                line_main = 0.0f;
                on_line = 0;
            }
            line_main += need + (on_line > 0 ? pl->gap : 0.0f);
            on_line++;
        }
        if (on_line > 0) {
            float line_cross = 0.0f;
            loop_r =
                flush_wrap_line(items, line_start, item_count - line_start, pl, content_min_x,
                                content_min_y, content_main, scroll, cross_cursor, &line_cross);
            if (YETTY_IS_ERR(loop_r)) {
                goto cleanup;
            }
        }
    }

cleanup:
    free(items);
    if (YETTY_IS_ERR(loop_r)) {
        return YETTY_ERR(yetty_ycore_void, "layout_node: child", loop_r);
    }
    return YETTY_OK_VOID();
}

struct yetty_ycore_void_result yetty_ygui_layout_compute(struct yetty_yclass_object *root,
                                                         struct yetty_ycore_rectangle root_rect)
{
    if (!root) {
        return YETTY_ERR(yetty_ycore_void, "yetty_ygui_layout_compute: NULL root");
    }
    return layout_node(root, root_rect);
}
