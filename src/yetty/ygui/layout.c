/*
 * ygui-layout.c — flex layout pass for the new toolkit.
 *
 * Algorithm summary:
 *
 *   For each container:
 *     1. Compute its content box (rect minus padding).
 *     2. Walk in-flow children:
 *        - "main size" = layout.width (row) or layout.height (column)
 *          clamped to [min, max]. Unset (-1.0f) treated as 0 baseline,
 *          flex_grow >0 then distributes free space.
 *        - "cross size" = layout.height (row) or layout.width (column),
 *          clamped. Unset means "auto" — uses parent cross-content for
 *          align=stretch, else 0 baseline.
 *     3. Sum children base sizes + gaps; free_space = content_main - sum.
 *        Distribute free_space proportionally to flex_grow / flex_shrink.
 *     4. Position children main-axis according to justify_content.
 *     5. Position children cross-axis according to align (parent default)
 *        / align (per-child override is not yet supported in this port —
 *        per-child align lives in a follow-up).
 *     6. Recurse into each child.
 *
 * Restricted feature set (current port):
 *   - No wrap (single-line flex).
 *   - No absolute positioning (every child is in-flow).
 *   - No percent sizing (px only).
 *   - No baseline alignment.
 *   - No margins on widgets (use padding on the parent + gap instead).
 *
 * These are intentional simplifications — the new toolkit will grow
 * each feature back in as widgets need them.
 */

#include "internal.h"

#include <yetty/ygui/widget.h>

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
 * sizes along its own direction (+ gaps), and the max child size on the
 * cross axis. Recurses so nested vbox/tree_node stacks size bottom-up.
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
                float cmain = row ? cw : ch;
                float ccross = row ? ch : cw;
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

/* Resolve a child's preferred main-axis size + cross-axis size into the
 * given container, before flex grow/shrink distribution. */
struct child_axes {
    float main_pref;
    float cross_pref;
    float main_min;
    float main_max;
    float cross_min;
    float cross_max;
    float grow;
    float shrink;
};

YETTY_YRESULT_DECLARE(child_axes, struct child_axes);

static struct child_axes_result resolve_child_axes(const struct yetty_ygui_layout *parent_layout,
                                                   struct yetty_yclass_object *child)
{
    struct yetty_ygui_layout_const_ptr_result child_layout_res =
        yetty_ygui_widget_layout_get(child);
    YETTY_RETURN_IF_ERR(child_axes, child_layout_res, "resolve_child_axes: layout_get");
    const struct yetty_ygui_layout *child_layout = child_layout_res.value;
    struct child_axes a;
    int row = direction_is_row(parent_layout);
    float authored_main = row ? child_layout->width : child_layout->height;
    if (row) {
        a.main_pref = child_layout->width >= 0.0f ? child_layout->width : 0.0f;
        a.cross_pref = child_layout->height >= 0.0f ? child_layout->height : 0.0f;
        a.main_min = child_layout->min_width;
        a.main_max = child_layout->max_width;
        a.cross_min = child_layout->min_height;
        a.cross_max = child_layout->max_height;
    } else {
        a.main_pref = child_layout->height >= 0.0f ? child_layout->height : 0.0f;
        a.cross_pref = child_layout->width >= 0.0f ? child_layout->width : 0.0f;
        a.main_min = child_layout->min_height;
        a.main_max = child_layout->max_height;
        a.cross_min = child_layout->min_width;
        a.cross_max = child_layout->max_width;
    }
    a.grow = child_layout->flex_grow;
    a.shrink = child_layout->flex_shrink;
    /* When the child doesn't author its main size and won't grow to fill,
     * derive an intrinsic content size so nested containers (tree_node,
     * vbox-of-rows) stack instead of collapsing to a zero rect. Grow
     * children keep a 0 base — they expand to fill regardless. Childless
     * leaves measure 0 (unless they carry padding, e.g. a tree_node whose
     * header lives in padding_top), so this is safe for plain widgets. */
    if (authored_main < 0.0f && a.grow <= 0.0f) {
        float mw = 0.0f, mh = 0.0f;
        struct yetty_ycore_void_result m_res = measure_size(child, &mw, &mh);
        YETTY_RETURN_IF_ERR(child_axes, m_res, "resolve_child_axes: measure_size");
        a.main_pref = row ? mw : mh;
    }
    a.main_pref = clamp_size(a.main_pref, a.main_min, a.main_max);
    return YETTY_OK(child_axes, a);
}

/* Count visible in-flow children + sum their preferred main sizes +
 * flex factors. */
struct flex_summary {
    int child_count;
    float sum_main_pref;
    float sum_grow;
    float sum_shrink;
};

YETTY_YRESULT_DECLARE(flex_summary, struct flex_summary);

static struct flex_summary_result summarize_children(struct yetty_yclass_object *parent,
                                                     const struct yetty_ygui_layout *parent_layout)
{
    struct flex_summary s = {0};
    struct yetty_yclass_object_ptr_result child_res = yetty_ygui_widget_first_child(parent);
    YETTY_RETURN_IF_ERR(flex_summary, child_res, "summarize_children: first_child");
    for (struct yetty_yclass_object *c = child_res.value; c;) {
        struct yetty_ygui_layout_const_ptr_result child_layout_res =
            yetty_ygui_widget_layout_get(c);
        YETTY_RETURN_IF_ERR(flex_summary, child_layout_res, "summarize_children: layout_get");
        const struct yetty_ygui_layout *cl = child_layout_res.value;
        /* Hidden subtrees and absolutely-positioned children skip flex. */
        if (!cl->hidden && !cl->absolute) {
            struct child_axes_result axes_res = resolve_child_axes(parent_layout, c);
            YETTY_RETURN_IF_ERR(flex_summary, axes_res, "summarize_children: resolve_child_axes");
            s.child_count++;
            s.sum_main_pref += axes_res.value.main_pref;
            s.sum_grow += axes_res.value.grow;
            s.sum_shrink += axes_res.value.shrink;
        }
        struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
        YETTY_RETURN_IF_ERR(flex_summary, next_res, "summarize_children: next_sibling");
        c = next_res.value;
    }
    return YETTY_OK(flex_summary, s);
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
    float content_main = direction_is_row(pl) ? content_w : content_h;
    float content_cross = direction_is_row(pl) ? content_h : content_w;

    struct flex_summary_result sum_res = summarize_children(node, pl);
    YETTY_RETURN_IF_ERR(yetty_ycore_void, sum_res, "layout_node: summarize_children");
    struct flex_summary sum = sum_res.value;
    /* Total gap consumed between flex children (zero when no flex children
     * exist). Absolute children still need to be placed, so this function
     * cannot early-return on `child_count == 0`. */
    float total_gap = sum.child_count > 1 ? pl->gap * (float)(sum.child_count - 1) : 0.0f;
    float consumed = sum.sum_main_pref + total_gap;
    float free_space = content_main - consumed;

    /* Distribute free_space via grow if positive, shrink if negative. */
    float grow_unit = 0.0f, shrink_unit = 0.0f;
    if (free_space > 0.0f && sum.sum_grow > 0.0f) {
        grow_unit = free_space / sum.sum_grow;
    } else if (free_space < 0.0f && sum.sum_shrink > 0.0f) {
        shrink_unit = free_space / sum.sum_shrink;
    }

    /* Two-pass: first compute sizes, then place using justify. The size
     * arrays are allocated for the actual flex-child count; absolute
     * children are sized inline above and skipped here. */
    float total_main = 0.0f;
    int idx = 0;
    float *main_sizes = NULL;
    float *cross_sizes = NULL;
    if (sum.child_count > 0) {
        main_sizes = calloc((size_t)sum.child_count, sizeof(*main_sizes));
        cross_sizes = calloc((size_t)sum.child_count, sizeof(*cross_sizes));
        if (!main_sizes || !cross_sizes) {
            free(main_sizes);
            free(cross_sizes);
            return YETTY_ERR(yetty_ycore_void, "layout_node: alloc sizes");
        }
    }

    struct yetty_ycore_void_result loop_r = YETTY_OK_VOID();
    {
        struct yetty_yclass_object_ptr_result iter_res = yetty_ygui_widget_first_child(node);
        if (YETTY_IS_ERR(iter_res)) {
            loop_r = YETTY_ERR(yetty_ycore_void, "layout_node: first_child", iter_res);
            goto cleanup;
        }
        for (struct yetty_yclass_object *c = iter_res.value; c;) {
            struct yetty_ygui_layout_const_ptr_result child_layout_res =
                yetty_ygui_widget_layout_get(c);
            if (YETTY_IS_ERR(child_layout_res)) {
                loop_r =
                    YETTY_ERR(yetty_ycore_void, "layout_node: child layout_get", child_layout_res);
                goto cleanup;
            }
            const struct yetty_ygui_layout *cl = child_layout_res.value;
            if (cl->hidden) {
                /* folded away — no rect, no recurse */
            } else if (cl->absolute) {
                /* Place absolute children directly at (pos_x, pos_y) inside
                 * parent content box. Skip flex bookkeeping. */
                struct yetty_ycore_rectangle crect;
                float aw = cl->width >= 0.0f ? cl->width : 0.0f;
                float ah = cl->height >= 0.0f ? cl->height : 0.0f;
                crect.min.x = content_min_x + cl->pos_x;
                crect.min.y = content_min_y + cl->pos_y;
                crect.max.x = crect.min.x + aw;
                crect.max.y = crect.min.y + ah;
                loop_r = layout_node(c, crect);
                if (YETTY_IS_ERR(loop_r)) {
                    goto cleanup;
                }
            } else {
                struct child_axes_result axes_res = resolve_child_axes(pl, c);
                if (YETTY_IS_ERR(axes_res)) {
                    loop_r =
                        YETTY_ERR(yetty_ycore_void, "layout_node: resolve_child_axes", axes_res);
                    goto cleanup;
                }
                struct child_axes a = axes_res.value;
                float ms = a.main_pref;
                if (grow_unit > 0.0f) {
                    ms += a.grow * grow_unit;
                } else if (shrink_unit < 0.0f) {
                    ms += a.shrink * shrink_unit;
                }
                ms = clamp_size(ms, a.main_min, a.main_max);
                if (ms < 0.0f) {
                    ms = 0.0f;
                }

                /* Cross size — start from preferred. If align=stretch and the
                 * preferred is 0 (auto), use content_cross. Then clamp. */
                float cs = a.cross_pref;
                if (cs == 0.0f && pl->align == YETTY_YGUI_ALIGN_STRETCH) {
                    cs = content_cross;
                }
                cs = clamp_size(cs, a.cross_min, a.cross_max);

                main_sizes[idx] = ms;
                cross_sizes[idx] = cs;
                total_main += ms;
                idx++;
            }
            struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
            if (YETTY_IS_ERR(next_res)) {
                loop_r = YETTY_ERR(yetty_ycore_void, "layout_node: next_sibling", next_res);
                goto cleanup;
            }
            c = next_res.value;
        }
    }

    int placed_count = idx;
    float total_with_gaps = total_main + total_gap;

    /* Justify on the main axis. */
    float main_offset = 0.0f;
    float gap_between = pl->gap;
    if (free_space > 0.0f && grow_unit == 0.0f) {
        /* No grow children consumed the free space — distribute via
         * justify-content. */
        switch (pl->justify) {
        case YETTY_YGUI_JUSTIFY_CENTER:
            main_offset = (content_main - total_with_gaps) * 0.5f;
            break;
        case YETTY_YGUI_JUSTIFY_END:
            main_offset = content_main - total_with_gaps;
            break;
        case YETTY_YGUI_JUSTIFY_SPACE_BETWEEN:
            if (placed_count > 1) {
                gap_between += (content_main - total_with_gaps) / (float)(placed_count - 1);
            }
            break;
        case YETTY_YGUI_JUSTIFY_START:
        default:
            break;
        }
    }

    /* Place children. A scrolling container carries a main-axis scroll
     * offset; subtract it so its in-flow children slide toward the
     * content-box origin. 0 for non-scrolling nodes. */
    idx = 0;
    struct yetty_ycore_float_result scroll_res = yetty_ygui_widget_scroll_main_get(node);
    if (YETTY_IS_ERR(scroll_res)) {
        loop_r = YETTY_ERR(yetty_ycore_void, "layout_node: scroll_main_get", scroll_res);
        goto cleanup;
    }
    float cursor_main = main_offset - scroll_res.value;
    {
        struct yetty_yclass_object_ptr_result iter_res = yetty_ygui_widget_first_child(node);
        if (YETTY_IS_ERR(iter_res)) {
            loop_r = YETTY_ERR(yetty_ycore_void, "layout_node: place first_child", iter_res);
            goto cleanup;
        }
        for (struct yetty_yclass_object *c = iter_res.value; c && idx < placed_count;) {
            struct yetty_ygui_layout_const_ptr_result child_layout_res =
                yetty_ygui_widget_layout_get(c);
            if (YETTY_IS_ERR(child_layout_res)) {
                loop_r = YETTY_ERR(yetty_ycore_void, "layout_node: place child layout_get",
                                   child_layout_res);
                goto cleanup;
            }
            const struct yetty_ygui_layout *cl = child_layout_res.value;
            /* Hidden children were skipped in the sizing pass (no idx
             * consumed); absolute children were already placed above. */
            if (!cl->hidden && !cl->absolute) {
                float ms = main_sizes[idx];
                float cs = cross_sizes[idx];

                /* Cross-axis offset depending on parent align. */
                float cross_offset = 0.0f;
                switch (pl->align) {
                case YETTY_YGUI_ALIGN_CENTER:
                    cross_offset = (content_cross - cs) * 0.5f;
                    break;
                case YETTY_YGUI_ALIGN_END:
                    cross_offset = content_cross - cs;
                    break;
                case YETTY_YGUI_ALIGN_STRETCH:
                    /* cs already = content_cross (computed above). */
                    cross_offset = 0.0f;
                    break;
                case YETTY_YGUI_ALIGN_START:
                default:
                    cross_offset = 0.0f;
                    break;
                }

                struct yetty_ycore_rectangle crect;
                if (direction_is_row(pl)) {
                    crect.min.x = content_min_x + cursor_main;
                    crect.min.y = content_min_y + cross_offset;
                    crect.max.x = crect.min.x + ms;
                    crect.max.y = crect.min.y + cs;
                } else {
                    crect.min.x = content_min_x + cross_offset;
                    crect.min.y = content_min_y + cursor_main;
                    crect.max.x = crect.min.x + cs;
                    crect.max.y = crect.min.y + ms;
                }

                loop_r = layout_node(c, crect);
                if (YETTY_IS_ERR(loop_r)) {
                    goto cleanup;
                }

                cursor_main += ms + gap_between;
                idx++;
            }
            struct yetty_yclass_object_ptr_result next_res = yetty_ygui_widget_next_sibling(c);
            if (YETTY_IS_ERR(next_res)) {
                loop_r = YETTY_ERR(yetty_ycore_void, "layout_node: place next_sibling", next_res);
                goto cleanup;
            }
            c = next_res.value;
        }
    }

cleanup:
    free(main_sizes);
    free(cross_sizes);
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
